#include "offload_util.h"
#include "offload_host.h"
#include "ompt_host.h"
#include "ompt_buffer_host.h"

const ompt_target_lib_info_t *ompt_info;

void __ompt_target_initialize()
{
    static int ompt_target_initialized = 0;
    static mutex_t lock;

    mutex_locker_t locker(lock);
    if (ompt_target_initialized == 0) {
        ompt_target_initialized = 1;

        ompt_info = ompt_target_initialize(&__ompt_recording_start, &__ompt_recording_stop);
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
    Tracer& tracer = engine.get_tracer();
    tracer.set_callbacks(request, complete);
    tracer.start();
    return 0;
}

int __ompt_recording_stop(int device_id) {
    Engine& engine = mic_engines[device_id % mic_engines_total];
    ompt_target_info_t& target_info = engine.get_target_info();
    Tracer& tracer = engine.get_tracer();
    tracer.stop();

    return 0;
}
