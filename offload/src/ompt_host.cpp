#include "offload_common.h"
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

static inline uint64_t increment_id(uint64_t *ptr)
{
#ifndef TARGET_WINNT
    return __sync_fetch_and_add(ptr, 1);
#else // TARGET_WINNT
    return _InterlockedIncrement(ptr);
#endif // TARGET_WINNT
}

ompt_target_activity_id_t ompt_target_activity_id_new()
{
    static uint64_t ompt_target_activity_id = 1;
    return increment_id(&ompt_target_activity_id);
}
