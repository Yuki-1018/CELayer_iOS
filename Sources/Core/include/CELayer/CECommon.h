#ifndef CELAYER_CE_COMMON_H
#define CELAYER_CE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define CE_PAGE_SIZE 4096u
#define CE_MAX_PATH 260u
#define CE_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef uint32_t CEAddress;
typedef uint32_t CEHandle;

typedef enum CEStatus {
    CE_OK = 0,
    CE_ERROR_INVALID_ARGUMENT,
    CE_ERROR_BAD_FORMAT,
    CE_ERROR_UNSUPPORTED,
    CE_ERROR_OUT_OF_MEMORY,
    CE_ERROR_ADDRESS_CONFLICT,
    CE_ERROR_ACCESS_VIOLATION,
    CE_ERROR_NOT_FOUND,
    CE_ERROR_IO,
    CE_ERROR_LIMIT,
    CE_ERROR_HALTED
} CEStatus;

typedef enum CEProtection {
    CE_PROT_NONE = 0,
    CE_PROT_READ = 1u << 0,
    CE_PROT_WRITE = 1u << 1,
    CE_PROT_EXEC = 1u << 2
} CEProtection;

const char *CEStatusString(CEStatus status);

#if defined(__cplusplus)
}
#endif
#endif
