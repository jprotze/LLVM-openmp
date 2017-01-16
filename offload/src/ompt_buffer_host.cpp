#include "coi/coi_client.h"
#include "compiler_if_host.h"
#include "offload_engine.h"
#include "ompt_common.h"
#include <common/COIEvent_common.h>
#include "ompt_buffer_host.h"
#include <iostream>

extern Engine*  mic_engines;
extern uint32_t mic_engines_total;

#define COICHECK(res) if(res != COI_SUCCESS) \
std::cerr << "COI ERROR: "  << __FILE__ << ": " <<  __LINE__ << ": " \
          << COIResultGetName(res) << std::endl;


Tracer::Tracer() : m_proc(NULL), m_device_id(-1), m_tracing(0), m_paused(0),
               m_funcs_inited(0), m_signal_threads_busy(true) {
    pthread_mutex_init(&m_mutex_pause, NULL);
    pthread_mutex_init(&m_mutex_pipeline, NULL);

    // Prepare and start the the signal thread
    pthread_mutex_init(&m_signal_thread_mutex, NULL);
    pthread_cond_init(&m_signal_thread_cond, NULL);
    pthread_attr_init(&m_signal_thread_attr);
    pthread_attr_setstacksize(&m_signal_thread_attr, 24*1024);
}

Tracer:: ~Tracer(){
    // Stop the signal threads
    pthread_mutex_lock(&m_signal_thread_mutex);
    m_signal_threads_busy = false;
    pthread_mutex_unlock(&m_signal_thread_mutex);

    // clean up singal thread
    pthread_mutex_destroy(&m_signal_thread_mutex);
    pthread_attr_destroy(&m_signal_thread_attr);
    pthread_cond_destroy(&m_signal_thread_cond);
}

COIPIPELINE Tracer::get_pipeline() {
    COIPIPELINE pipeline;
    pthread_mutex_lock(&m_mutex_pipeline);
    Engine& engine = mic_engines[m_device_id % mic_engines_total];
    Tracer& tracer = engine.get_tracer();
    pipeline = engine.get_pipeline();
    pthread_mutex_unlock(&m_mutex_pipeline);
    return pipeline;
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

    // start signal threads
    pthread_create(&m_signal_requested_thread, &m_signal_thread_attr,
        Tracer::signal_requested_helper, &m_device_id);
    pthread_create(&m_signal_truncated_thread, &m_signal_thread_attr,
        Tracer::signal_truncated_helper, &m_device_id);

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

void* Tracer::signal_requested_helper(void* data) {
    Engine& engine = mic_engines[*(uint64_t*)data % mic_engines_total];
    Tracer& tracer = engine.get_tracer();

    OFFLOAD_OMPT_TRACE(3, "Signal requested thread started for device %ld\n",
        *(uint64_t*)data% mic_engines_total);

    tracer.signal_requested();

    pthread_exit(NULL);
}

/**
 * Signals the device that the buffer has been allocated.
 */
void* Tracer::signal_requested() {
    uint32_t num_signaled;
    uint32_t signaled_indices[MAX_OMPT_THREADS];
    COIPIPELINE pipeline;
    COIRESULT result;

    // Since this is a signal worker thread, it get his own pipeline.
    // I think this is not required at all. However, it does not really
    // hurt here.
    pthread_mutex_lock(&m_mutex_pipeline);
    COICHECK(COI::PipelineCreate(
        m_proc,
        NULL,
        0,
        &pipeline
    ));
    pthread_mutex_unlock(&m_mutex_pipeline);

    OFFLOAD_OMPT_TRACE(3, "Request pipeline: %lx\n", pipeline);

    //pthread_mutex_lock(&m_signal_thread_mutex);
    while(m_signal_threads_busy) {

        result = COIEventWait(
            MAX_OMPT_THREADS,              // Number of events to wait for
            m_request_events,              // Event handles
            -1,                            // Wait indefinitely
            false,                         // Wait for all events
            &num_signaled, signaled_indices // Number of events signaled
                                           // and their indices
        );

        // event cancelation means that the runtime shuts down,
        // thus we destroy the signal thread right now.
        if(result == COI_EVENT_CANCELED) {
            pthread_exit(NULL);
        } else if(result == COI_SUCCESS) {
            OFFLOAD_OMPT_TRACE(4,
                "Successfully waited for %d request events (signaled sink side)\n",
                 num_signaled);
        } else {
            COICHECK(result);
        }

        // re-register all received events (they are one-shot events)
        for(int i=0; i< num_signaled; i++){
            int tid = signaled_indices[i];

            OFFLOAD_OMPT_TRACE(4, "Processesing REQUEST event %d\n",
                m_request_events[tid].opaque[0]);

            pthread_mutex_lock(&m_mutex_pause);
            if(!m_paused) {
            // register event
            COICHECK(COIEventRegisterUserEvent(&m_request_events[tid]));
            // transfer registered event to device
            COICHECK(COI::BufferWrite(
                m_request_event_buffer,
                tid*sizeof(COIEVENT),  // the offset (we only want to register 
                                       // event for one thread) 
                                       // TODO: Is this safe when call "start" again?
                &m_request_events[tid],
                sizeof(COIEVENT),
                COI_COPY_USE_DMA,
                0, NULL,
                COI_EVENT_SYNC));
            }
            pthread_mutex_unlock(&m_mutex_pause);

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

            OFFLOAD_OMPT_TRACE(3, "Created tracing buffer from %p: [%ld] bytes\n",
                                tdata[tid].host_ptr, tdata[tid].host_size);


            // We need to pass the tid and the amount of allocated data to
            // the device.
            uint64_t misc_data[] = {tid, tdata[tid].host_size};
            OFFLOAD_OMPT_TRACE(3, "Send signal buffer_allocated\n");
            COI_ACCESS_FLAGS flags = COI_SINK_WRITE;
            COICHECK(COI::PipelineRunFunction(
                pipeline, ompt_funcs[c_ompt_func_signal_buffer_allocated],
                1, &tdata[tid].buffer, &flags,
                0, NULL,
                &misc_data, 2 * sizeof(uint64_t),
                NULL, 0,
                NULL));
        }
    }
}

void* Tracer::signal_truncated_helper(void* data) {
    Engine& engine = mic_engines[*(uint64_t*)data % mic_engines_total];
    Tracer& tracer = engine.get_tracer();

    OFFLOAD_OMPT_TRACE(3, "Signal truncated thread started for device %ld\n",
        *(uint64_t*)data% mic_engines_total);

    tracer.signal_truncated();

    pthread_exit(NULL);
}

/**
 * Signals the device that the buffer has been truncated.
 */
void* Tracer::signal_truncated() {
    uint32_t num_signaled;
    uint32_t signaled_indices[MAX_OMPT_THREADS];
    COIPIPELINE pipeline;
    COIRESULT result;

    // Since this is a signal worker thread, it get his own pipeline.
    // I think this is not required at all. However, it does not really
    // hurt here.
    pthread_mutex_lock(&m_mutex_pipeline);
    COICHECK(COI::PipelineCreate(
        m_proc,
        NULL,
        0,
        &pipeline
    ));
    pthread_mutex_unlock(&m_mutex_pipeline);
 
    OFFLOAD_OMPT_TRACE(3, "Truncate pipeline: %lx\n", pipeline);

    //pthread_mutex_lock(&m_signal_thread_mutex);
    while(m_signal_threads_busy) {

        result = COIEventWait(
            MAX_OMPT_THREADS,              // Number of events to wait for
            m_full_events,                 // Event handles
            -1,                            // Wait indefinitely
            false,                         // Wait for all events
            &num_signaled, signaled_indices // Number of events signaled
                                           // and their indices
        );

        // event cancelation means that the runtime shuts down,
        // thus we destroy the signal thread.
        if(result == COI_EVENT_CANCELED) {
            pthread_exit(NULL);
        } else if(result == COI_SUCCESS) {
            OFFLOAD_OMPT_TRACE(4, 
                "Successfully waited for %d request events (signaled sink side)\n",
                 num_signaled);
        } else {
            COICHECK(result);
        }

        // re-register all received events (they are one-shot events)
        for(int i=0; i< num_signaled; i++){
            int tid = signaled_indices[i];
            uint64_t pos;
            uint64_t size;

            OFFLOAD_OMPT_TRACE(4, "Processesing FULL event %d\n",
                m_full_events[tid].opaque[0]);

            pthread_mutex_lock(&m_mutex_pause);
            if(!m_paused) {
            // register event
            COICHECK(COIEventRegisterUserEvent(&m_full_events[tid]));
            // transfer registered event to device
            COICHECK(COI::BufferWrite(
                m_full_event_buffer,
                tid*sizeof(COIEVENT), // the offset (we only want to register
                                      // event for one thread)
                                      // TODO: Is this safe when call "start" again?
                &m_full_events[tid],
                sizeof(COIEVENT),
                COI_COPY_USE_DMA,
                0, NULL,
                COI_EVENT_SYNC));
            }
            pthread_mutex_unlock(&m_mutex_pause);

            // Read the current buffer position to determine the amount of
            // data which need to be transferred
            read_buffer(m_pos_buffer, &m_buffer_pos[tid], sizeof(uint64_t), tid*sizeof(uint64_t));
            size = m_buffer_pos[tid]*sizeof(ompt_record_t);

            // Transfer the collected data to the host
            read_buffer(tdata[tid].buffer, tdata[tid].host_ptr, size);

            complete_callback(tdata[tid].host_ptr, m_device_id, size);

            OFFLOAD_OMPT_TRACE(3, 
                "Received %d events and send signal buffer_truncated (tid=%d)\n",
                m_buffer_pos[tid], tid);

            COICHECK(COI::PipelineRunFunction(
                pipeline, ompt_funcs[c_ompt_func_signal_buffer_truncated],
                0, NULL, NULL,
                0, NULL,
                &tid, sizeof(uint64_t),
                NULL, 0,
                NULL));
            COICHECK(COI::BufferDestroy(tdata[tid].buffer));

        }
    }
}


void Tracer::read_buffer(COIBUFFER buffer, void *target_host,
    uint64_t bytes, uint64_t offset) {
    // receive data from sink
    COICHECK(COI::BufferRead(
        buffer,
        offset,
        target_host,
        bytes,
        COI_COPY_USE_DMA,
        0, NULL,
        COI_EVENT_SYNC));
}

void Tracer::start() {
    m_tracing = 1;
    OFFLOAD_OMPT_TRACE(3, "Start OMPT Tracer\n");

    if (!m_paused) {
        for(int i=0; i<MAX_OMPT_THREADS; i++){
            COICHECK(COIEventRegisterUserEvent(&m_request_events[i]));
            COICHECK(COIEventRegisterUserEvent(&m_full_events[i]));
        }

        // create buffer for request and full event
        // and attach to liboffload process
        COICHECK(COI::BufferCreateFromMemory(
                sizeof(COIEVENT) * MAX_OMPT_THREADS,
                COI_BUFFER_NORMAL,
                0,
                m_request_events,
                1, &m_proc,
                &m_request_event_buffer));
    
        COICHECK(COI::BufferCreateFromMemory(
                sizeof(COIEVENT) * MAX_OMPT_THREADS,
                COI_BUFFER_NORMAL,
                0,
                m_full_events,
                1, &m_proc,
                &m_full_event_buffer));

        COICHECK(COI::BufferCreateFromMemory(
                sizeof(uint64_t) * MAX_OMPT_THREADS,
                COI_BUFFER_NORMAL,
                0,
                m_buffer_pos,
                1, &m_proc,
                &m_pos_buffer));

        init_functions();

        COIPIPELINE pipeline = get_pipeline();

        // transfer the event buffers to device
        COIBUFFER event_buffers[] = { m_request_event_buffer, m_full_event_buffer, m_pos_buffer };
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
    OFFLOAD_OMPT_TRACE(3, "Stop OMPT Tracer\n");
    if (m_tracing) {
        m_tracing = 0;

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
        COICHECK(COI::BufferDestroy(m_request_event_buffer));
        COICHECK(COI::BufferDestroy(m_full_event_buffer));

        //TODO: this should be a postprocessing step instead of doing for each stop
        for(int i=0; i<MAX_OMPT_THREADS; i++){
            COICHECK(COIEventUnregisterUserEvent(m_request_events[i]));
            COICHECK(COIEventUnregisterUserEvent(m_full_events[i]))
        }

        if(final) {
            //TODO: clean up?
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

