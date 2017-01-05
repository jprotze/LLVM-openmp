#include "coi/coi_client.h"
#include "compiler_if_host.h"
#include "offload_engine.h"
#include <common/COIEvent_common.h>
#include "ompt_buffer_host.h"
#include <iostream>

extern Engine*  mic_engines;
extern uint32_t mic_engines_total;

#define COICHECK(res) if(res != COI_SUCCESS) \
std::cerr << "COI ERROR: "  << __FILE__ << ": " <<  __LINE__ << ": " \
          << COIResultGetName(res) << std::endl;


Tracer::Tracer() : m_proc(NULL), m_device_id(-1), m_tracing(0), m_paused(0),
               m_funcs_inited(0) {
    pthread_mutex_init(&m_mutex_pause, NULL);

    m_signal_thread_info.busy = true;

    // Prepare and start the the signal thread
    pthread_mutex_init(&m_signal_thread_mutex, NULL);
    pthread_cond_init(&m_signal_thread_cond, NULL);
    pthread_attr_init(&m_signal_thread_attr);
    pthread_attr_setstacksize(&m_signal_thread_attr, 24*1024);
}

Tracer:: ~Tracer(){
    // Stop the signal thread
    pthread_mutex_lock(&m_signal_thread_mutex);
    m_signal_thread_info.busy=false;
    pthread_mutex_unlock(&m_signal_thread_mutex);

    // clean up singal thread
    pthread_mutex_destroy(&m_signal_thread_mutex);
    pthread_attr_destroy(&m_signal_thread_attr);
    pthread_cond_destroy(&m_signal_thread_cond);
}

COIPIPELINE Tracer::get_pipeline() {
    Engine& engine = mic_engines[m_device_id % mic_engines_total];
    Tracer& tracer = engine.get_tracer();
    return engine.get_pipeline();
}

void Tracer::init_functions() {
    if (m_funcs_inited) {
        return;
    }

    COIRESULT result = COI::ProcessGetFunctionHandles(
                m_proc,
                c_ompt_funcs_total,
                ompt_func_names,
                ompt_funcs
        );

    COICHECK(result);

    if (result == COI_SUCCESS) {
        m_funcs_inited = 1;
    }

    assert(m_device_id != -1);

    // start signal thread
    pthread_create(&m_signal_thread, &m_signal_thread_attr, Tracer::signal_buffer_helper, &m_device_id);

    COICHECK(COIRegisterNotificationCallback(m_proc, Tracer::notification_callback_helper, &m_device_id));
}

uint64_t Tracer::get_time() {
    uint64_t ret;

    init_functions();

    COIPIPELINE pipeline = get_pipeline();
    COI_ACCESS_FLAGS flags = COI_SINK_READ;

    // Run the actual function
    COICHECK(COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_target_get_time],
            0, NULL, NULL,
            0, NULL,
            NULL, 0,
            &ret, sizeof(uint64_t),
            NULL));

    return ret;
}

void* Tracer::signal_buffer_helper(void* data) {
    Engine& engine = mic_engines[*(uint64_t*)data % mic_engines_total];
    Tracer& tracer = engine.get_tracer();

    OFFLOAD_DEBUG_TRACE(5, "Signal thread started for device %ld\n", 
        *(uint64_t*)data% mic_engines_total);

    tracer.signal_buffer_op();

    pthread_exit(NULL);
}

/**
 * Signals that the buffer has been truncated to the device.
 */
void* Tracer::signal_buffer_op() {
    COIPIPELINE pipeline;

    // Since this is the signal worker thread, it get his own pipeline.
    // I think this is not required at all. However, it does not really
    // hurt here.
    COICHECK(COI::PipelineCreate(
        m_proc,
        NULL,
        0,
        &pipeline
    ));
 
    pthread_mutex_lock(&m_signal_thread_mutex);
    while(m_signal_thread_info.busy) {

        pthread_cond_wait(&m_signal_thread_cond, &m_signal_thread_mutex);

        if(m_signal_thread_info.handle == c_ompt_func_signal_buffer_truncated) {
            OFFLOAD_DEBUG_TRACE(5, "Send signal buffer_truncated\n");
            COICHECK(COI::PipelineRunFunction(
                pipeline, ompt_funcs[c_ompt_func_signal_buffer_truncated],
                0, NULL, NULL,
                0, NULL,
                NULL, 0, 
                NULL, 0,
                NULL));
            COICHECK(COI::BufferDestroy(m_signal_thread_info.buffer));

        } else if(m_signal_thread_info.handle == c_ompt_func_signal_buffer_allocated){
            int tid = m_signal_thread_info.thread_data.value;
            OFFLOAD_DEBUG_TRACE(5, "Send signal buffer_allocated\n");
            COI_ACCESS_FLAGS flags = COI_SINK_WRITE;
            COICHECK(COI::PipelineRunFunction(
                pipeline, ompt_funcs[c_ompt_func_signal_buffer_allocated],
                1, &tdata[tid].buffer, &flags,
                0, NULL,
                &tdata[tid].host_size, sizeof(uint64_t),
                NULL, 0,
                NULL));

        } else {
            std::cerr << "ERROR: Unexpected signal handle. Exit.\n";
            exit(1);
        }

    }
    pthread_mutex_unlock(&m_signal_thread_mutex);
    COIPipelineDestroy(pipeline);
}


void Tracer::read_buffer(COIBUFFER buffer, void *target_host, size_t bytes) {
    // receive data from sink
    COICHECK(COI::BufferRead(
        buffer,
        0,
        target_host,
        bytes,
        COI_COPY_USE_DMA,
        0, NULL,
        COI_EVENT_SYNC));
}

void Tracer::register_event(COIBUFFER buffer, COIEVENT* event) {

    pthread_mutex_lock(&m_mutex_pause);
    if(!m_paused) {
    // register event
    COICHECK(COIEventRegisterUserEvent(event));
    // transfer registered event to device
    COICHECK(COI::BufferWrite(
        buffer,
        0,
        event,
        sizeof(COIEVENT),
        COI_COPY_USE_DMA,
        0, NULL,
        COI_EVENT_SYNC));
    }
    pthread_mutex_unlock(&m_mutex_pause);
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
        ompt_id_t tid;
        read_buffer(m_tid_buffer, &tid, sizeof(ompt_id_t));


        read_buffer(tdata[tid].buffer,
                tdata[tid].host_ptr,
                tdata[tid].host_size);
        register_event(full_event_buffer,
                &m_full_event);

        OFFLOAD_DEBUG_TRACE(5, "Read tracing buffer from device and filled to %p: [%ld] bytes\n",
                            tdata[tid].host_ptr, tdata[tid].host_size);

        complete_callback(tdata[tid].host_ptr, device_id, tdata[tid].host_size);

        // We have to call a COIPipelineRunFunction here, but this would invoke
        // this callback again causing a deadlock. We can avoid
        // this by calling the COIPipelineRunFunction asynchronously in an own thread.
        // The built-in COI mechanism for an asynchronous call does not work here!
        pthread_mutex_lock(&m_signal_thread_mutex);
        m_signal_thread_info.device_id = device_id;
        m_signal_thread_info.thread_data.value = tid;
        m_signal_thread_info.buffer = tdata[tid].buffer;
        m_signal_thread_info.handle = c_ompt_func_signal_buffer_truncated;

        // Wake up the signal thread
        pthread_cond_signal(&m_signal_thread_cond);
        pthread_mutex_unlock(&m_signal_thread_mutex);
    }

    // buffer request event
    else if (!memcmp(&in_Event, &m_request_event, sizeof(COIEVENT))) {
        // read tid
        ompt_id_t tid;
        read_buffer(m_tid_buffer, &tid, sizeof(ompt_id_t));


        // The missing lock here might be critical! Since we lock on the device
        // side, it should be ok.
        request_callback(&tdata[tid].host_ptr, &tdata[tid].host_size);

        // create buffer and attach to liboffload process
        COICHECK(COI::BufferCreateFromMemory(
                tdata[tid].host_size,
                COI_BUFFER_NORMAL,
                0,
                tdata[tid].host_ptr,
                1, &m_proc,
                &tdata[tid].buffer));

        OFFLOAD_DEBUG_TRACE(5, "Created tracing buffer from %p: [%ld] bytes\n", 
                            tdata[tid].host_ptr, tdata[tid].host_size);

        // re-register event (it is an one-shot event)
        register_event(request_event_buffer,
                &(m_request_event));

        // We have to call a COIPipelineRunFunction here, but this would invoke
        // this callback again causing a deadlock. We can avoid
        // this by calling the COIPipelineRunFunction asynchronously in an own thread.
        // The built-in COI mechanism for an asynchronous call does not work here!
        pthread_mutex_lock(&m_signal_thread_mutex);
        m_signal_thread_info.device_id = device_id;
        m_signal_thread_info.thread_data.value = tid;
        m_signal_thread_info.buffer = tdata[tid].buffer;
        m_signal_thread_info.handle = c_ompt_func_signal_buffer_allocated;

        // Wake up the signal thread
        pthread_cond_signal(&m_signal_thread_cond);
        pthread_mutex_unlock(&m_signal_thread_mutex);
    }

    else std::cerr << "UNKOWN USER SIG\n";
}

void Tracer::start() {
    m_tracing = 1;
    OFFLOAD_DEBUG_TRACE(5, "Start OMPT Tracer\n");

    if (!m_paused) {
        COICHECK(COIEventRegisterUserEvent(&m_request_event));
        COICHECK(COIEventRegisterUserEvent(&m_full_event))


        // create buffer for request and full event
        // and attach to liboffload process
        COICHECK(COI::BufferCreateFromMemory(
                sizeof(COIEVENT),
                COI_BUFFER_NORMAL,
                0,
                &m_request_event,
                1, &m_proc,
                &request_event_buffer));
    
        COICHECK(COI::BufferCreateFromMemory(
                sizeof(COIEVENT),
                COI_BUFFER_NORMAL,
                0,
                &m_full_event,
                1, &m_proc,
                &full_event_buffer));

        COICHECK(COI::BufferCreate(
                sizeof(ompt_id_t),
                COI_BUFFER_NORMAL,
                0,
                NULL,
                1, &m_proc,
                &m_tid_buffer));

        init_functions();

        COIPIPELINE pipeline = get_pipeline();

        // transfer the event buffers to device
        COIBUFFER event_buffers[] = { request_event_buffer, full_event_buffer, m_tid_buffer };
        COI_ACCESS_FLAGS event_buffers_flags[] = { COI_SINK_WRITE, COI_SINK_WRITE, COI_SINK_WRITE };
        COICHECK(COI::PipelineRunFunction(
                pipeline, ompt_funcs[c_ompt_func_start_tracing],
                3, event_buffers, event_buffers_flags,
                0, NULL,
                NULL, 0,
                NULL, 0,
                NULL));

        } else {
            m_paused = 0;
            COIPIPELINE pipeline = get_pipeline();
            COICHECK(COI::PipelineRunFunction(
                pipeline, ompt_funcs[c_ompt_func_restart_tracing],
                0, NULL, NULL,
                0, NULL,
                NULL, 0,
                NULL, 0,
                NULL));

        }
}

void Tracer::stop(bool final) {
    OFFLOAD_DEBUG_TRACE(5, "Stop OMPT Tracer\n");
    if (m_tracing) {
        m_tracing = 0;

        // We first interrupt the tracing on the device.
        pause();

        // Once the tracing was interrupted, we can flush the buffers.
        flush();

        m_paused = 0;

        COIPIPELINE pipeline = get_pipeline();
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

        // Clean up
        COICHECK(COI::BufferDestroy(request_event_buffer));
        COICHECK(COI::BufferDestroy(full_event_buffer));
        COICHECK(COI::BufferDestroy(m_tid_buffer));

        COICHECK(COIEventUnregisterUserEvent(m_request_event));
        COICHECK(COIEventUnregisterUserEvent(m_full_event))

        if(final) {
            COICHECK(COIUnregisterNotificationCallback(m_proc, Tracer::notification_callback_helper));
        }
    }
}

void Tracer::pause() {
    pthread_mutex_lock(&m_mutex_pause);
    m_paused = 1;
    pthread_mutex_unlock(&m_mutex_pause);
    COIPIPELINE pipeline = get_pipeline();
    COICHECK(COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_pause_tracing],
            0, NULL, NULL,
            0, NULL,
            NULL, 0,
            NULL, 0,
            NULL));

}

void Tracer::flush() {
    COIPIPELINE pipeline = get_pipeline();

    OFFLOAD_DEBUG_TRACE(5, "Flush OMPT buffer\n");

    for (std::map<uint64_t, thread_data_t>::iterator it = tdata.begin();
            it != tdata.end(); ++it) {
        uint64_t pos;
        COICHECK(COI::PipelineRunFunction(
            pipeline, ompt_funcs[c_ompt_func_get_buffer_pos],
            0, NULL, NULL,
            0, NULL,
            &it->first, sizeof(uint64_t),
            &pos, sizeof(uint64_t),
            NULL));

        if (pos != 0) {
            read_buffer(it->second.buffer,
                        tdata[it->first].host_ptr,
                        pos * sizeof(ompt_record_t));

            complete_callback(
                        tdata[it->first].host_ptr,
                        m_device_id,
                        pos * sizeof(ompt_record_t));

            COICHECK(COI::BufferDestroy(tdata[it->first].buffer));
            OFFLOAD_DEBUG_TRACE(5,"Tracing buffer of thread %d destroyed.\n", it->first);
        }
    }
}
