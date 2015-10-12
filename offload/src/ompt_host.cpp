#include "offload_util.h"
#include "offload_host.h"
#include "ompt_host.h"
#include "ompt_buffer_host.h"

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
                               &ompt_get_task_id, &ompt_get_task_frame,
                               &__ompt_recording_start, &__ompt_recording_stop);
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

ompt_task_id_t __ompt_target_task_id_new()
{
    static uint64_t ompt_target_task_id = 1;
    return increment_id(&ompt_target_task_id);
}

int __ompt_recording_start(
        int device_id,
        ompt_target_buffer_request_callback_t request,
        ompt_target_buffer_complete_callback_t complete) {
    // get corresponding engine
    Engine& engine = mic_engines[device_id % mic_engines_total];
    ompt_target_info_t& target_info = engine.get_target_info();
    target_info.request_callback = request;
    target_info.complete_callback = complete;
    __ompt_target_start_tracing(device_id);
    return 0;
}

int __ompt_recording_stop(int device_id) {
    __ompt_target_stop_tracing(device_id);
    return 0;
}
