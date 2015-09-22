#include "offload_util.h"
#include "ompt_host.h"

ompt_enabled_t ompt_enabled;
ompt_get_target_callback_t __ompt_get_target_callback;
ompt_get_task_id_t ompt_get_task_id;
ompt_get_task_frame_t ompt_get_task_frame;

void __ompt_target_initialize()
{
    static int ompt_target_initialized = 0;
    static mutex_t lock;

    mutex_locker_t locker(lock);
    if (ompt_target_initialized == 0) {
        ompt_target_initialized = 1;

        ompt_target_initialize(&ompt_enabled, &__ompt_get_target_callback,
                               &ompt_get_task_id, &ompt_get_task_frame);
    }
}
