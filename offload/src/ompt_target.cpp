#include "offload.h"
#include "compiler_if_target.h"
#include <common/COIEvent_common.h>
#include <sys/time.h>
#include <ompt.h>

uint64_t target_get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return 1000000 * tv.tv_sec + tv.tv_usec;
}

COINATIVELIBEXPORT
void ompt_target_get_time(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
   *((uint64_t*) return_data) = target_get_time();
   return_data_len = sizeof(uint64_t);
}
