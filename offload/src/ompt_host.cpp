#include "offload_util.h"
#include "ompt_host.h"

const ompt_target_lib_info_t *ompt_info;

void __ompt_target_initialize()
{
    static int ompt_target_initialized = 0;
    static mutex_t lock;

    mutex_locker_t locker(lock);
    if (ompt_target_initialized == 0) {
        ompt_target_initialized = 1;

        ompt_info = ompt_target_initialize();
    }
}
