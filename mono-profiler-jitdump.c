#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>

#define DO_EXPORT __attribute__ ((__visibility__ ("default")))

#ifdef USE_REAL_MONO

#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/domain-internals.h>
#include <mono/metadata/profiler.h>
#include <mono/utils/mono-time.h>

#else

/**
 * Mono compatible declarations, to avoid needing mono internals to build
 */
typedef struct _MonoDomain MonoDomain;
typedef struct _MonoMethod MonoMethod;
typedef struct _MonoProfiler MonoProfiler;
typedef struct _MonoProfilerDesc *MonoProfilerHandle;

typedef struct _MonoJitInfo {
    union {
        MonoMethod* method;  /* method, image, aot_info, tramp_info -- always method for us */
    } d;
    struct _JitInfo* next_jit_code_or_tombstone;
    void* code_start;
    uint32_t unwind_info;
    int code_size;
    /* lots of other stuff we don't care about */
} MonoJitInfo;

#define MONO_API /* nothing */

extern char* mono_method_full_name(MonoMethod* method, int signature);
extern MonoProfilerHandle mono_profiler_create(MonoProfiler *prof);

typedef void (*MonoProfileJitDoneFunc)(void* prof, MonoMethod *method, MonoJitInfo *jinfo);
typedef void (*MonoProfileJitCodeBufferFunc)(void* prof, void *buffer, uint64_t size, int /*MonoProfilerCodeBufferType*/ type, const void *data);
typedef void (*MonoProfileDomainFunc)(void* prof, MonoDomain *domain);

extern void mono_profiler_set_jit_done_callback(MonoProfilerHandle handle, MonoProfileJitDoneFunc callback);
extern void mono_profiler_set_jit_code_buffer_callback(MonoProfilerHandle handle, MonoProfileJitCodeBufferFunc callback);
extern void mono_profiler_set_domain_unloading_callback(MonoProfilerHandle handle, MonoProfileDomainFunc callback);
extern void mono_profiler_set_domain_unloaded_callback(MonoProfilerHandle handle, MonoProfileDomainFunc callback);

#endif

#ifndef EM_ARM
#define EM_ARM (0x28)
#define EM_AARCH64 (0xb7)
#define EM_X86_64 (0x3e)
#endif

static FILE *perf_dump_file;
static pthread_mutex_t perf_dump_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *perf_dump_mmap_addr = MAP_FAILED;
static uint32_t perf_dump_pid;

DO_EXPORT void mono_profiler_init_jitdump(const char *desc);

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
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t elf_mach;
    uint32_t pad1; // padding for alignment
    uint32_t pid;
    uint64_t timestamp;
    uint64_t flags;
} FileHeader;

typedef struct
{
    uint32_t id;
    uint32_t total_size;
    uint64_t timestamp;
} RecordHeader;

typedef struct
{
    RecordHeader header;
    uint32_t pid;
    uint32_t tid;
    uint64_t vma;
    uint64_t code_addr;
    uint64_t code_size;
    uint64_t code_index;
    // Null terminated function name
    // Native code
} JitCodeLoadRecord;

struct _MonoProfiler {
    MonoProfilerHandle handle;
};

static struct _MonoProfiler jitdump_profiler;

static uint64_t
clock_get_time_ns()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return ((uint64_t) tp.tv_sec * 1000000000) + (uint64_t) tp.tv_nsec;
#if false
    kern_return_t ret;
    mach_timespec_t mach_ts;
    do {
        ret = clock_get_time (clk_id, &mach_ts);
    } while (ret == KERN_ABORTED);

    //if (ret != KERN_SUCCESS)
    //	g_error ("%s: clock_get_time () returned %d", __func__, ret);
    printf ("%s: clock_get_time () returned %d\n", __func__, ret);

    return ((uint64_t) mach_ts.tv_sec * 1000000000) + (uint64_t) mach_ts.tv_nsec;
#endif
}

static void
init_jit_dump_file_header(FileHeader *header)
{
    header->magic = JIT_DUMP_MAGIC;
    header->version = JIT_DUMP_VERSION;
    header->total_size = sizeof(FileHeader);
    header->elf_mach = ELF_MACHINE;
    header->pad1 = 0;
    header->pid = perf_dump_pid;
    header->timestamp = clock_get_time_ns();
    header->flags = 0;
}

static void
mono_enable_jit_dump(void)
{
    if (perf_dump_pid == 0)
        perf_dump_pid = getpid();
    
    if (!perf_dump_file) {
        char name[64];
        FileHeader header;
        memset(&header, 0, sizeof (header));

        snprintf(name, sizeof(name), "/tmp/jit-%d.dump", perf_dump_pid);
        unlink(name);

        // samply hooks open to see if something is writing to a jit*.dump file
        perf_dump_file = fopen(name, "w");
        
        init_jit_dump_file_header(&header);
        if (perf_dump_file) {
            fwrite(&header, sizeof (header), 1, perf_dump_file);
        }
    }
}

void
mono_emit_jit_dump(MonoJitInfo *jinfo, void* code)
{
    static uint64_t code_index;
    
    if (perf_dump_file) {
        // note: this allocates
        char* name = mono_method_full_name(jinfo->d.method, 0 /* signature */);
        size_t nameLen = strlen(name);
        //size_t nameLen = strlen (jinfo->d.method->name);
        unsigned long long tid;
        pthread_threadid_np(NULL, &tid);

        JitCodeLoadRecord record;
        memset(&record, 0, sizeof (record));
        record.header.id = JIT_CODE_LOAD;
        record.header.timestamp = clock_get_time_ns();
        record.pid = perf_dump_pid;
        record.tid = (uint32_t) tid;
        record.header.total_size = sizeof (record) + nameLen + 1 + jinfo->code_size;
        record.vma = (uint64_t)jinfo->code_start;
        record.code_addr = (uint64_t)jinfo->code_start;
        record.code_size = (uint64_t)jinfo->code_size;

        pthread_mutex_lock(&perf_dump_mutex);
        {
            record.code_index = ++code_index;
            // TODO? write debugInfo and unwindInfo immediately before the JitCodeLoadRecord (while lock is held).
            record.header.timestamp = clock_get_time_ns();
            
            fwrite(&record, sizeof (record), 1, perf_dump_file);
            //fwrite (jinfo->d.method->name, nameLen + 1, 1, perf_dump_file);
            fwrite(name, nameLen + 1, 1, perf_dump_file);
            fwrite(code, jinfo->code_size, 1, perf_dump_file);
        }
        pthread_mutex_unlock(&perf_dump_mutex);

        free(name);
    }
}

static void
mono_jit_dump_cleanup (void)
{
    if (perf_dump_mmap_addr != MAP_FAILED)
        munmap (perf_dump_mmap_addr, sizeof(FileHeader));
    if (perf_dump_file)
        fclose (perf_dump_file);
}

static void
method_jitted (void *prof, MonoMethod *method, MonoJitInfo *ji)
{
    mono_emit_jit_dump(ji, ji->code_start);
}

static void
domain_unloaded (void *prof, MonoDomain *domain)
{
    /* TODO */
}

void
mono_profiler_init_jitdump(const char *desc)
{
    MonoProfilerHandle handle = jitdump_profiler.handle = mono_profiler_create(&jitdump_profiler);

    printf("## Initialized jitdump profiler module\n");
    mono_enable_jit_dump();

    mono_profiler_set_jit_done_callback(handle, method_jitted);
    mono_profiler_set_domain_unloaded_callback(handle, domain_unloaded);
}