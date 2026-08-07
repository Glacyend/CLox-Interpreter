#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// #define DEBUG_PRINT_CODE
// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_STRESS_GC
// #define DEBUG_LOG_GC

#ifndef DISABLE_NAN_BOXING
    #if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
        #define PLATFORM_64_BIT
    #endif

    #if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
        #define PLATFORM_LITTLE_ENDIAN
    #elifdef _WIN32
        #define PLATFORM_LITTLE_ENDIAN
    #endif

    #ifdef __STDC_IEC_559__
        #define PLATFORM_IEEE_754
    #elif !defined(__STDC_IEC_559__) && (defined(_WIN32) || defined(__unix__) || defined(__APPLE__))
        #define PLATFORM_IEEE_754
    #endif

    #if defined(PLATFORM_64_BIT) && defined(PLATFORM_LITTLE_ENDIAN) && defined(PLATFORM_IEEE_754)
        #define NAN_BOXING
    #endif
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
