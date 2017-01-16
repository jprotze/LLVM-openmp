#ifndef OMPT_COMMON_H_INCLUDED
#define OMPT_COMMON_H_INCLUDED

#include "offload_common.h"

#define MAX_OMPT_THREADS 240

#define COICHECK(res) if(res != COI_SUCCESS) \
std::cerr << "COI ERROR: "  << __FILE__ << ": " <<  __LINE__ << ": " \
          << COIResultGetName(res) << std::endl;

#define OFFLOAD_OMPT_TRACE(trace_level, ...) \
    if (console_enabled >= trace_level) { \
        OFFLOAD_DEBUG_PRINT_PREFIX(); \
        printf("[OMPT] "); \
        printf(__VA_ARGS__); \
        fflush(NULL); \
    }

#endif
