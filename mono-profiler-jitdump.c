#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/domain-internals.h>
#include <mono/metadata/profiler.h>
#include <mono/utils/mono-time.h>

#include <sys/mman.h>
#include <sys/syscall.h>

#ifndef EM_ARM
#define EM_ARM (0x28)
#define EM_AARCH64 (0xb7)
#define EM_X86_64 (0x3e)
#endif

static FILE *perf_dump_file;
static mono_mutex_t perf_dump_mutex;
static void *perf_dump_mmap_addr = MAP_FAILED;
static guint32 perf_dump_pid;
static clockid_t clock_id = CLOCK_MONOTONIC;

enum {
    JIT_DUMP_MAGIC = 0x4A695444,
    JIT_DUMP_VERSION = 1,
    // on macos, do different if x86 or arm
#if defined(__x86_64__)
    ELF_MACHINE = EM_X86_64,
#elif defined(__arm64__)
    ELF_MACHINE = EM_AARCH64,
#else
    #error not supported
#endif
    JIT_CODE_LOAD = 0
};

typedef struct
{
    guint32 magic;
    guint32 version;
    guint32 total_size;
    guint32 elf_mach;
    guint32 pad1; // padding for alignment
    guint32 pid;
    guint64 timestamp;
    guint64 flags;
} FileHeader;

typedef struct
{
    guint32 id;
    guint32 total_size;
    guint64 timestamp;
} RecordHeader;

typedef struct
{
    RecordHeader header;
    guint32 pid;
    guint32 tid;
    guint64 vma;
    guint64 code_addr;
    guint64 code_size;
    guint64 code_index;
    // Null terminated function name
    // Native code
} JitCodeLoadRecord;

static void add_file_header_info (FileHeader *header);
static void add_basic_JitCodeLoadRecord_info (JitCodeLoadRecord *record);

static guint64
clock_get_time_ns (mono_clock_id_t clk_id)
{
    struct timespec tp;
    clock_gettime(clk_id, &tp);
    return ((guint64) tp.tv_sec * 1000000000) + (guint64) tp.tv_nsec;
#if false
    kern_return_t ret;
    mach_timespec_t mach_ts;
    do {
        ret = clock_get_time (clk_id, &mach_ts);
    } while (ret == KERN_ABORTED);

    //if (ret != KERN_SUCCESS)
    //	g_error ("%s: clock_get_time () returned %d", __func__, ret);
    printf ("%s: clock_get_time () returned %d\n", __func__, ret);

    return ((guint64) mach_ts.tv_sec * 1000000000) + (guint64) mach_ts.tv_nsec;
#endif
}

static void
mono_enable_jit_dump (void)
{
    if (perf_dump_pid == 0)
        perf_dump_pid = getpid();
    
    if (!perf_dump_file) {
        char name [64];
        FileHeader header;
        memset (&header, 0, sizeof (header));

        mono_os_mutex_init (&perf_dump_mutex);
        
        g_snprintf (name, sizeof (name), "/tmp/jit-%d.dump", perf_dump_pid);
        unlink (name);

        // samply hooks open to see if something is writing to a jit*.dump file
        perf_dump_file = fopen (name, "w");
        
        add_file_header_info (&header);
        if (perf_dump_file) {
            fwrite (&header, sizeof (header), 1, perf_dump_file);
        }
        
        mono_os_mutex_unlock (&perf_dump_mutex);
    }
}

void
add_file_header_info (FileHeader *header)
{
    header->magic = JIT_DUMP_MAGIC;
    header->version = JIT_DUMP_VERSION;
    header->total_size = sizeof(FileHeader);
    header->elf_mach = ELF_MACHINE;
    header->pad1 = 0;
    header->pid = perf_dump_pid;
    header->timestamp = clock_get_time_ns (clock_id);
    header->flags = 0;
}

void
mono_emit_jit_dump (MonoJitInfo *jinfo, gpointer code)
{
    static uint64_t code_index;
    
    if (perf_dump_file) {
        JitCodeLoadRecord record;
        char* name = mono_method_full_name(jinfo->d.method, FALSE);
        size_t nameLen = strlen(name);
        //size_t nameLen = strlen (jinfo->d.method->name);
        memset (&record, 0, sizeof (record));
        
        add_basic_JitCodeLoadRecord_info (&record);
        record.header.total_size = sizeof (record) + nameLen + 1 + jinfo->code_size;
        record.vma = (guint64)jinfo->code_start;
        record.code_addr = (guint64)jinfo->code_start;
        record.code_size = (guint64)jinfo->code_size;

        mono_os_mutex_lock (&perf_dump_mutex);
        
        record.code_index = ++code_index;
        
        // TODO: write debugInfo and unwindInfo immediately before the JitCodeLoadRecord (while lock is held).
        
        record.header.timestamp = clock_get_time_ns (clock_id);
        
        fwrite (&record, sizeof (record), 1, perf_dump_file);
        //fwrite (jinfo->d.method->name, nameLen + 1, 1, perf_dump_file);
        fwrite (name, nameLen + 1, 1, perf_dump_file);
        fwrite (code, jinfo->code_size, 1, perf_dump_file);

        free(name);

        mono_os_mutex_unlock (&perf_dump_mutex);
    }
}

void
add_basic_JitCodeLoadRecord_info (JitCodeLoadRecord *record)
{
    record->header.id = JIT_CODE_LOAD;
    record->header.timestamp = clock_get_time_ns (clock_id);
    record->pid = perf_dump_pid;
    unsigned long long tid;
    pthread_threadid_np(NULL, &tid);
    record->tid = (guint32) tid;
}

static void
mono_jit_dump_cleanup (void)
{
    if (perf_dump_mmap_addr != MAP_FAILED)
        munmap (perf_dump_mmap_addr, sizeof(FileHeader));
    if (perf_dump_file)
        fclose (perf_dump_file);
}

struct _MonoProfiler {
    MonoProfilerHandle handle;
};

static struct _MonoProfiler jitdump_profiler;

static void
method_jitted (MonoProfiler *prof, MonoMethod *method, MonoJitInfo *ji)
{
    mono_emit_jit_dump(ji, ji->code_start);
}

MONO_API void
mono_profiler_init_jitdump (const char *desc);

void
mono_profiler_init_jitdump (const char *desc)
{
    MonoProfilerHandle handle = jitdump_profiler.handle = mono_profiler_create (&jitdump_profiler);
    //if (mono_jit_aot_compiling ())
    //    return;

    printf("=============== INIT JITDUMP\n");
    mono_enable_jit_dump();

    mono_profiler_set_jit_done_callback (handle, method_jitted);
}