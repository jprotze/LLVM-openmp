#include "coi/coi_client.h"
#include "compiler_if_host.h"
#include "offload_engine.h"
#include <common/COIEvent_common.h>
#include "ompt_buffer_host.h"
#include <pthread.h>

extern Engine*  mic_engines;
extern uint32_t mic_engines_total;


COIPIPELINE Tracer::create_pipeline() {
    COIPIPELINE pipeline;

    COIRESULT result;
    result = COI::PipelineCreate(
        m_proc,
        NULL,
        0,
        &pipeline
    );

    if (result != COI_SUCCESS) {
        printf("ERROR: Could not create pipeline\n");
    }

    return pipeline;
}

uint64_t Tracer::get_time() {
    uint64_t ret;

    // TODO: This will be done in every call and should be moved
    // in an initialization function or somehwere else.
    COIRESULT result = COI::ProcessGetFunctionHandles(
                m_proc,
                c_ompt_funcs_total,
                ompt_func_names,
                ompt_funcs
        );

    if (result != COI_SUCCESS) {
        printf("ERROR: Getting function handle failed\n");
    }

    COIPIPELINE pipeline = create_pipeline();
    COI_ACCESS_FLAGS flags = COI_SINK_READ;

    // Run the actual function
    result = COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_target_get_time],
            0, NULL, NULL,
            0, NULL,
            NULL, 0,
            &ret, sizeof(uint64_t),
            NULL);

    if (result != COI_SUCCESS) {
        printf("ERROR: Running pipeline function failed on target time request\n");
    }

    COI::PipelineDestroy(pipeline);

    return ret;
}

void* Tracer::signal_buffer_allocated_helper(void *data) {
    ompt_buffer_info_t buffer_info = *((ompt_buffer_info_t*) data);
    free(data);

    Engine& engine = mic_engines[buffer_info.device_id % mic_engines_total];
    Tracer& tracer = engine.get_tracer();
    return tracer.signal_buffer_allocated(buffer_info.tid);
}

/**
 * Signals the allocation of the requested buffer to the device.
 */
void* Tracer::signal_buffer_allocated(int tid) {
    COIRESULT result;
    COIPIPELINE pipeline = create_pipeline();

    COI_ACCESS_FLAGS flags = COI_SINK_WRITE;
    result = COI::PipelineRunFunction(
        pipeline, ompt_funcs[c_ompt_func_signal_buffer_allocated],
        1, &tdata[tid].buffer, &flags,
        0, NULL,
        &tdata[tid].host_size, sizeof(uint64_t),
        NULL, 0,
        NULL);

    if (result != COI_SUCCESS) {
        printf("ERROR: Running pipeline function failed on transfer buffer\n");
    }

    COI::PipelineDestroy(pipeline);
}


void* Tracer::signal_buffer_truncated_helper(void *data) {
    ompt_buffer_info_t buffer_info = *((ompt_buffer_info_t*) data);
    free(data);

    Engine& engine = mic_engines[buffer_info.device_id % mic_engines_total];
    Tracer& tracer = engine.get_tracer();
    return tracer.signal_buffer_truncated();
}

/**
 * Signals that the buffer has been truncated to the device.
 */
void* Tracer::signal_buffer_truncated() {
    COIRESULT result;

    COIPIPELINE pipeline = create_pipeline();

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

    COI::PipelineDestroy(pipeline);
}


void Tracer::read_buffer(COIBUFFER buffer, void *target_host, size_t bytes) {
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

void Tracer::register_event(COIBUFFER buffer, COIEVENT* event) {
    // register event
    COIEventRegisterUserEvent(event);
    // transfer registered event to device
    COI::BufferWrite(
        buffer,
        0,
        event,
        sizeof(COIEVENT),
        COI_COPY_USE_DMA,
        0, NULL,
        COI_EVENT_SYNC);
}

void Tracer::notification_callback_helper(COI_NOTIFICATIONS in_type, COIPROCESS in_Process, COIEVENT in_Event, const void* in_UserData) {
    // Is this a user event?
    if (in_type == USER_EVENT_SIGNALED) {
        // the user data only contains the device id
        int device_id = *((int*)in_UserData);

        // we need to get the tracer belonging to the device and call it's
        // object-specific callback
        Engine& engine = mic_engines[device_id % mic_engines_total];
        Tracer& tracer = engine.get_tracer();
        tracer.notification_callback(in_type, in_Process, in_Event, in_UserData);
    }
}

void Tracer::notification_callback(COI_NOTIFICATIONS in_type, COIPROCESS in_Process, COIEVENT in_Event, const void* in_UserData) {
    // the user data only contains the device id
    int device_id = *((int*)in_UserData);

    // buffer full event
    if (!memcmp(&in_Event, &m_full_event, sizeof(COIEVENT))) {
        // read tid
        ompt_thread_id_t tid;
        read_buffer(m_tid_buffer, &tid, sizeof(ompt_thread_id_t));

        read_buffer(tdata[tid].buffer,
                tdata[tid].host_ptr,
                tdata[tid].host_size);
        register_event(full_event_buffer,
                &m_full_event);

        complete_callback(tdata[tid].host_ptr, device_id, tdata[tid].host_size);

        // We have to call a COIPipelineRunFunction here, but this would invoke
        // this callback again causing a deadlock. We can avoid
        // this by calling the COIPipelineRunFunction asynchronously in an own thread.
        // The built-in COI mechanism for an asynchronous call does not work here!
        ompt_buffer_info_t *thread_buffer_info = (ompt_buffer_info_t*) malloc(sizeof(ompt_buffer_info_t));
        thread_buffer_info->device_id = device_id;
        thread_buffer_info->tid = tid;

        pthread_t my_thread;
        pthread_create(&my_thread, NULL, Tracer::signal_buffer_truncated_helper, (void*) thread_buffer_info);

    }

    // buffer request event
    if (!memcmp(&in_Event, &m_request_event, sizeof(COIEVENT))) {
        // read tid
        ompt_thread_id_t tid;
        read_buffer(m_tid_buffer, &tid, sizeof(ompt_thread_id_t));

        request_callback(&tdata[tid].host_ptr, &tdata[tid].host_size);

        // create buffer and attach to liboffload process
        COIRESULT result = COI::BufferCreateFromMemory(
                tdata[tid].host_size,
                COI_BUFFER_NORMAL,
                0,
                tdata[tid].host_ptr,
                1, &m_proc,
                &tdata[tid].buffer);

        if (result != COI_SUCCESS) {
            printf("ERROR: Creating COIBuffer failed\n");
            return;
        }

        register_event(request_event_buffer,
                &(m_request_event));

        // We have to call a COIPipelineRunFunction here, but this would invoke
        // this callback again causing a deadlock. We can avoid
        // this by calling the COIPipelineRunFunction asynchronously in an own thread.
        // The built-in COI mechanism for an asynchronous call does not work here!
        ompt_buffer_info_t *thread_buffer_info = (ompt_buffer_info_t*) malloc(sizeof(ompt_buffer_info_t));
        thread_buffer_info->device_id = device_id;
        thread_buffer_info->tid = tid;

        pthread_t my_thread;
        pthread_create(&my_thread, NULL, Tracer::signal_buffer_allocated_helper, (void*) thread_buffer_info);

    }
}

void Tracer::start() {
    m_tracing = 1;

    if (!m_paused) {
        COIEventRegisterUserEvent(&m_request_event);
        COIEventRegisterUserEvent(&m_full_event);

        COIRegisterNotificationCallback(m_proc, Tracer::notification_callback_helper, &m_device_id);

        // create buffer for request and full event
        // and attach to liboffload process
        COIRESULT result = COI::BufferCreateFromMemory(
                sizeof(COIEVENT),
                COI_BUFFER_NORMAL,
                0,
                &m_request_event,
                1, &m_proc,
                &request_event_buffer);
    
        result = COI::BufferCreateFromMemory(
                sizeof(COIEVENT),
                COI_BUFFER_NORMAL,
                0,
                &m_full_event,
                1, &m_proc,
                &full_event_buffer);

        result = COI::BufferCreate(
                sizeof(ompt_thread_id_t),
                COI_BUFFER_NORMAL,
                0,
                NULL,
                1, &m_proc,
                &m_tid_buffer);

        if (result != COI_SUCCESS) {
            printf("ERROR: Creating buffer failed, %d\n", result);
        }

        result = COI::ProcessGetFunctionHandles(
                m_proc,
                c_ompt_funcs_total,
                ompt_func_names,
                ompt_funcs
        );

        if (result != COI_SUCCESS) {
            printf("ERROR: Getting function handle failed\n");
        }

        COIPIPELINE pipeline = create_pipeline();

        // transfer the event buffers to device
        COIBUFFER event_buffers[] = { request_event_buffer, full_event_buffer, m_tid_buffer };
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

        } else {
            m_paused = 0;
            COIPIPELINE pipeline = create_pipeline();
            COIRESULT result = COI::PipelineRunFunction(
                pipeline, ompt_funcs[c_ompt_func_restart_tracing],
                0, NULL, NULL,
                0, NULL,
                NULL, 0,
                NULL, 0,
                NULL);

            if (result != COI_SUCCESS) {
                printf("ERROR: Running pipeline function failed\n");
            }

            COI::PipelineDestroy(pipeline);
        }
}

void Tracer::stop() {
    if (m_tracing) {
        m_tracing = 0;

        COIPIPELINE pipeline = create_pipeline();
        COIRESULT result = COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_stop_tracing],
            0, NULL, NULL,
            0, NULL,
            NULL, 0,
            NULL, 0,
            NULL);

        if (result != COI_SUCCESS) {
            printf("ERROR: Running pipeline function failed\n");
        }

        COI::PipelineDestroy(pipeline);

        flush();
    }
}

void Tracer::pause() {
    m_paused = 1;
    COIPIPELINE pipeline = create_pipeline();
    COIRESULT result = COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_stop_tracing],
            0, NULL, NULL,
            0, NULL,
            NULL, 0,
            NULL, 0,
            NULL);

    if (result != COI_SUCCESS) {
        printf("ERROR: Running pipeline function failed\n");
    }

    COI::PipelineDestroy(pipeline);
}

void Tracer::flush() {
    COIPIPELINE pipeline = create_pipeline();

    for (std::map<uint64_t, thread_data_t>::iterator it = tdata.begin();
            it != tdata.end(); ++it) {

        uint64_t pos;
        COIRESULT result = COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_get_buffer_pos],
            0, NULL, NULL,
            0, NULL,
            &it->first, sizeof(uint64_t),
            &pos, sizeof(uint64_t),
            NULL);

        if (pos != 0) {
            read_buffer(it->second.buffer,
                        tdata[it->first].host_ptr,
                        pos * sizeof(ompt_record_t));

            complete_callback(
                        tdata[it->first].host_ptr,
                        m_device_id,
                        pos * sizeof(ompt_record_t));
        }
    }

    COI::PipelineDestroy(pipeline);
}
