#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Definitions
enum {
    JIT_DUMP_MAGIC = 0x4A695444,
    JIT_DUMP_VERSION = 1,
    JIT_CODE_LOAD = 0
};

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t elf_mach;
    uint32_t pad1;
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
    // The actual function name and native code will follow after this struct in memory.
} JitCodeLoadRecord;

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <jitdump-file>\n", argv[0]);
        return 1;
    }

    FILE* file = fopen(argv[1], "rb");
    if (!file) {
        perror("Failed to open file");
        return 1;
    }

    FileHeader fileHeader;
    if (fread(&fileHeader, sizeof(FileHeader), 1, file) != 1) {
        perror("Failed to read file header");
        fclose(file);
        return 1;
    }

    // Validate header
    if (fileHeader.magic != JIT_DUMP_MAGIC || fileHeader.version != JIT_DUMP_VERSION) {
        fprintf(stderr, "Invalid jitdump file or unsupported version\n");
        fclose(file);
        return 1;
    }

    printf("JIT Dump File:\n");
    printf("Version: %u\n", fileHeader.version);
    printf("Machine: 0x%02x\n", fileHeader.elf_mach);
    printf("Total Size: %u\n", fileHeader.total_size);
    printf("PID: %u\n", fileHeader.pid);
    printf("Timestamp: %llu\n", fileHeader.timestamp);
    printf("Flags: %llu\n", fileHeader.flags);

    // Read and print all the records
    while (1) {
        JitCodeLoadRecord record;
        long pos = ftell(file);
        //if (fread(&record, sizeof(JitCodeLoadRecord) - sizeof(char*), 1, file) != 1) {
        if (fread(&record, sizeof(JitCodeLoadRecord), 1, file) != 1) {
            if (feof(file))
                break;
            perror("Failed to read record");
            fclose(file);
            return 1;
        }

        printf("\nRecord:\n");
        printf("ID: %u\n", record.header.id);
        printf("Total Size: %u\n", record.header.total_size);
        printf("Timestamp: %llu\n", record.header.timestamp);
        printf("PID: %u\n", record.pid);
        printf("TID: %u\n", record.tid);
        printf("VMA: 0x%16llx\n", record.vma);
        printf("Code Address: 0x%16llx\n", record.code_addr);
        //printf("Code Size: %llu\n", record.code_size);
        printf("Code Index: %llu\n", record.code_index);

        // Read and print the function name (null terminated)
        char funcName[256]; // Adjust size as needed
        if (!fgets(funcName, sizeof(funcName), file)) {
            perror("Failed to read function name");
            fclose(file);
            return 1;
        }
        printf("Function Name: %s\n", funcName);
        fseek(file, pos + record.header.total_size, SEEK_SET);
    }

    fclose(file);
    return 0;
}
