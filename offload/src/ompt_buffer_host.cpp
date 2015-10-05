#include "offload.h"
#include "compiler_if_host.h"
#include "coi/coi_client.h"
#include "ompt_buffer_host.h"
#include <pthread.h>
#include <common/COIEvent_common.h>

#define MIC_MAX_THREAD_NUM 240

extern Engine*  mic_engines;
extern uint32_t mic_engines_total;

// TODO: Maybe we should put the tracing definitions and functions in an own class.

enum {
    c_ompt_func_start_tracing = 0,
    c_ompt_func_stop_tracing,
    c_ompt_func_signal_buffer_allocated,
    c_ompt_func_signal_buffer_truncated,
    c_ompt_funcs_total
};

static const char* ompt_func_names[] = {
    "ompt_target_start_tracing",
    "ompt_target_stop_tracing",
    "ompt_signal_buffer_allocated",
    "ompt_signal_buffer_truncated"
};

typedef struct {
	ompt_thread_id_t tid;
	int device_id;
} ompt_buffer_info_t;

// handles to the OMPT target tracing functions
COIFUNCTION ompt_funcs[c_ompt_funcs_total];

/**
 * Signals the allocation of the requested buffer to the device.
 */
void *ompt_signal_buffer_allocated(void *data) {
    ompt_buffer_info_t* buffer_info = (ompt_buffer_info_t*) data;

    Engine& engine = mic_engines[buffer_info->device_id % mic_engines_total];
    COIPROCESS proc = engine.get_process();    

    ompt_target_info_t& target_info = engine.get_target_info();

    COIRESULT result;
    COIPIPELINE pipeline;
    result = COI::PipelineCreate(
        proc,
        NULL,
        0,
        &pipeline
    );

    if (result != COI_SUCCESS) {
        printf("ERROR: Could not create pipeline\n");
    }

    COI_ACCESS_FLAGS flags = COI_SINK_WRITE;
    result = COI::PipelineRunFunction(
        pipeline, ompt_funcs[c_ompt_func_signal_buffer_allocated],
        1, &target_info.buffers[buffer_info->tid], &flags,
        0, NULL,
        &target_info.host_size[buffer_info->tid], sizeof(uint64_t),    
        NULL, 0,
    NULL);

    if (result != COI_SUCCESS) {
        printf("ERROR: Running pipeline function failed on transfer buffer\n");
    }

    free(data);
    COI::PipelineDestroy(pipeline);
}

/**
 * Signals that the buffer has been truncated to the device.
 */
void *ompt_signal_buffer_truncated(void* data) {
    ompt_buffer_info_t* buffer_info = (ompt_buffer_info_t*) data;

    Engine& device = mic_engines[buffer_info->device_id % mic_engines_total];
    COIPROCESS proc = device.get_process();    
    COIRESULT result;

    COIPIPELINE pipeline;
    result = COI::PipelineCreate(
        proc,
        NULL,
        0,
        &pipeline
    );

    if (result != COI_SUCCESS) {
        printf("ERROR: Creating pipeline failed on signal full\n");
    }

    result = COI::PipelineRunFunction(
        pipeline, ompt_funcs[c_ompt_func_signal_buffer_truncated],
        0, NULL, NULL,
        0, NULL,
        NULL, 0,    
        NULL, 0,
        NULL);

    if (result != COI_SUCCESS) {
        printf("ERROR: Running pipeline function failed on signal full\n");
    }

    free(data);
    COI::PipelineDestroy(pipeline);
}


void __pull_event_buffer(COIBUFFER buffer, uint64_t tid, void *target_host, size_t bytes) {
    // receive data from sink
    COIRESULT result;
    result = COI::BufferRead(
        buffer,
        0,
        target_host,
        bytes,
        COI_COPY_USE_DMA,
        0, NULL,
        COI_EVENT_SYNC);
}

void __register_event(COIBUFFER buffer, uint64_t offset, COIEVENT* event) {
    // register event
    COIEventRegisterUserEvent(event);
    // transfer registered event to device
    COI::BufferWrite(
        buffer,
        offset * sizeof(COIEVENT),
        event,
        sizeof(COIEVENT),
        COI_COPY_USE_DMA,
        0, NULL,
        COI_EVENT_SYNC);
}

void __read_buffer(COIBUFFER buffer, uint64_t offset, void* dest, size_t size) {
    COI::BufferRead(
            buffer,
            offset * size,
            dest,
            size,
            COI_COPY_USE_DMA,
            0, NULL,
            COI_EVENT_SYNC);
}

/**
 * This callback is invoked for each COI notification.
 */
void notification_callback(COI_NOTIFICATIONS in_type, COIPROCESS in_Process, COIEVENT in_Event, const void* in_UserData) {
    // Is this a user event?
    if (in_type == USER_EVENT_SIGNALED) {
        // the user data only contains the device id
        int device_id = *((int*)in_UserData);    

        // we need to get the process created by liboffload
        Engine& engine = mic_engines[device_id % mic_engines_total];
        COIPROCESS proc = engine.get_process();    

        // get target info data
        ompt_target_info_t& target_info = engine.get_target_info();

        // buffer full event
        for (unsigned int i = 0; i < MIC_MAX_THREAD_NUM; i++) {
            if (!memcmp(&in_Event, &(target_info.full_events[i]), sizeof(COIEVENT))) {
                int tid = i;
                COIEVENT newevent;
                __pull_event_buffer(target_info.buffers[i],
                                    i,
                                    target_info.host_ptrs[i],
                                    target_info.host_size[i]);
                __register_event(target_info.full_event_buffer,
                                 i,
                                 &(target_info.full_events[i]));

                target_info.complete_callback(target_info.host_ptrs[i], device_id, target_info.host_size[i]);

                // We have to call a COIPipelineRunFunction here, but this would invoke
                // this callback again causing a deadlock. We can avoid
                // this by calling the COIPipelineRunFunction asynchronously in an own thread.
                // The built-in COI mechanism for an asynchronous call does not work here!
                ompt_buffer_info_t *thread_buffer_info = (ompt_buffer_info_t*) malloc(sizeof(ompt_buffer_info_t));
                thread_buffer_info->device_id = device_id;
                thread_buffer_info->tid = tid;    

                pthread_t my_thread;
                pthread_create(&my_thread, NULL, ompt_signal_buffer_truncated, (void*) thread_buffer_info);

                break;
            }
        }

        // buffer request event
        for (unsigned int i = 0; i < MIC_MAX_THREAD_NUM; i++) {
            if (!memcmp(&in_Event, &(target_info.request_events[i]), sizeof(COIEVENT))) {
                int tid = i;
                target_info.request_callback(&target_info.host_ptrs[tid], target_info.host_size + tid);

                // create buffer and attach to liboffload process
                COIRESULT result = COI::BufferCreateFromMemory(
                        target_info.host_size[tid],
                        COI_BUFFER_NORMAL,
                        0,
                        target_info.host_ptrs[tid],
                        1, &proc,
                        &target_info.buffers[tid]);

                if (result != COI_SUCCESS) {
                    printf("ERROR: Creating COIBuffer failed\n");
                    return;
                }
        
                // We have to call a COIPipelineRunFunction here, but this would invoke 
                // this callback again causing a deadlock. We can avoid
                // this by calling the COIPipelineRunFunction asynchronously in an own thread.
                // The built-in COI mechanism for an asynchronous call does not work here!
                ompt_buffer_info_t *thread_buffer_info = (ompt_buffer_info_t*) malloc(sizeof(ompt_buffer_info_t));
                thread_buffer_info->device_id = device_id;
                thread_buffer_info->tid = tid;    
    
                pthread_t my_thread;
                pthread_create(&my_thread, NULL, ompt_signal_buffer_allocated, (void*) thread_buffer_info);

                break;
            }
        }
    }
}

// Requests host memory from tool to copy back collected device callbacks
// and initializes on the host.
void __ompt_target_start_tracing(int device_id) {
    // we need to get the process created by liboffload
    Engine& engine = mic_engines[device_id % mic_engines_total];
    COIPROCESS proc = engine.get_process();    
    ompt_target_info_t& target_info = engine.get_target_info();
    target_info.device_id = device_id; // FIXME: ugly
    target_info.tracing = 1;

    for (unsigned int i=0; i < MIC_MAX_THREAD_NUM; i++) {
        COIEventRegisterUserEvent(&(target_info.request_events[i]));
        COIEventRegisterUserEvent(&(target_info.full_events[i]));
    }
    COIRegisterNotificationCallback(proc, notification_callback, &target_info.device_id);

    // create buffer for request and full event
    // array and attach to liboffload process
    COIRESULT result = COI::BufferCreateFromMemory(
            MIC_MAX_THREAD_NUM * sizeof(COIEVENT),
            COI_BUFFER_NORMAL,
            0,
            target_info.request_events,
            1, &proc,
            &target_info.request_event_buffer);
    
    result = COI::BufferCreateFromMemory(
            MIC_MAX_THREAD_NUM * sizeof(COIEVENT),
            COI_BUFFER_NORMAL,
            0,
            target_info.full_events,
            1, &proc,
            &target_info.full_event_buffer);

    // create buffer for positions
    result = COI::BufferCreateFromMemory(
            MIC_MAX_THREAD_NUM * sizeof(uint64_t),
            COI_BUFFER_NORMAL,
            0,
            target_info.pos,
            1, &proc,
            &target_info.buffer_pos);

    if (result != COI_SUCCESS) {
        printf("ERROR: Creating buffer failed\n");
    }

    result = COI::ProcessGetFunctionHandles(
            engine.get_process(),
            c_ompt_funcs_total,
            ompt_func_names,
            ompt_funcs
    );

    if (result != COI_SUCCESS) {
        printf("ERROR: Getting function handle failed\n");
    }

    COIPIPELINE pipeline;
    COI::PipelineCreate(
            proc,
            NULL,
            0,
            &pipeline
    );

    // transfer the event buffers to device
    COIBUFFER event_buffers[] = { target_info.request_event_buffer, target_info.full_event_buffer, target_info.buffer_pos };
    COI_ACCESS_FLAGS event_buffers_flags[] = { COI_SINK_WRITE, COI_SINK_WRITE, COI_SINK_WRITE };
    result = COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_start_tracing],
            3, event_buffers, event_buffers_flags,
            0, NULL,
            NULL, 0,
            NULL, 0,
            NULL);

    if (result != COI_SUCCESS) {
        printf("ERROR: Running pipeline function failed\n");
    }

    COI::PipelineDestroy(pipeline);
}

void __ompt_target_stop_tracing(int device_id) {
    // TODO
    //Engine& engine = mic_engines[device_id % mic_engines_total];
    //ompt_target_info_t& target_info = engine.get_target_info();
    //target_info.tracing = 0;
}
