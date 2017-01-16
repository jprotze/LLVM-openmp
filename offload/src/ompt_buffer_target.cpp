#include "offload.h"
#include "compiler_if_target.h"
#include "ompt_callback_target.h"
#include "ompt_common.h"
#include <common/COIEvent_common.h>
#include <sys/time.h>
#include <pthread.h>
#include <ompt.h>
#include <map>
#include "ompt_target.h"

ompt_get_thread_data_t my_ompt_get_thread_data;

COIEVENT* ompt_buffer_request_events;
COIEVENT* ompt_buffer_complete_events;

pthread_mutex_t mutex_tdata; //Remove this lock by using a TLS?

bool tracing = false;

typedef struct {
    ompt_record_t* event_buffer;
    uint64_t buffer_size;
} ompt_thread_data_buffer_t;

uint64_t* tdata_pos;

// TODO: Really use as global map? I would prefer a
// TLS here. Problem: How to flush all buffer?
std::map<uint64_t, ompt_thread_data_buffer_t> tdata;

// TODO: Thread arrays a bad because of false sharing effects.
// Better use some padding!
pthread_cond_t waiting_buffer_requests[MAX_OMPT_THREADS];
pthread_cond_t waiting_buffer_completes[MAX_OMPT_THREADS];
pthread_mutex_t mutex_waiting_buffer_requests[MAX_OMPT_THREADS];
pthread_mutex_t mutex_waiting_buffer_completes[MAX_OMPT_THREADS];
bool buffer_request_conditions[MAX_OMPT_THREADS];
bool buffer_full_conditions[MAX_OMPT_THREADS];

void __attribute__((constructor)) init(){
    for(int i=0; i<MAX_OMPT_THREADS; i++){
        buffer_request_conditions[i]=false;
        buffer_full_conditions[i]=false;
    }
}


inline void ompt_buffer_flush(ompt_id_t tid) {
    // Since we can not send any parameters with
    // the user signal we need to set the thread specific lock
    // By doing this we can read the tid safely from the host.
    pthread_mutex_lock(&mutex_waiting_buffer_completes[tid]);
    COICHECK(COIEventSignalUserEvent(ompt_buffer_complete_events[tid]));
    OFFLOAD_OMPT_TRACE(5, 
        "Send signal ompt_buffer_complete_event (tid=%d, buf_pos=%d)\n",
        tid, tdata_pos[tid]);
    // wait for tool truncating the buffer on host
    while (!buffer_full_conditions[tid]) {
        pthread_cond_wait(&waiting_buffer_completes[tid],
            &mutex_waiting_buffer_completes[tid]);
    }
    buffer_full_conditions[tid] = false;

    tdata_pos[tid] = 0;

    pthread_mutex_lock(&mutex_tdata);

    // Delete the collected data for the current tid. This will signal
    // a buffer_request_event as soon as a further event occured.
    tdata.erase(tid);

    pthread_mutex_unlock(&mutex_tdata);

    pthread_mutex_unlock(&mutex_waiting_buffer_completes[tid]);
}

inline void ompt_buffer_request(ompt_id_t tid) {
    bool data_exits;

    data_exits = tdata.count(tid);

    if (!data_exits) {
        // request buffer, send signal to host
        pthread_mutex_lock(&mutex_waiting_buffer_requests[tid]);
        tdata_pos[tid] = 0;
        COICHECK(COIEventSignalUserEvent(ompt_buffer_request_events[tid]));
        OFFLOAD_OMPT_TRACE(3, 
            "Send signal ompt_buffer_request_event (tid=%d)\n", tid);

        // wait for tool allocating buffer on host
        while (!buffer_request_conditions[tid]) {
            pthread_cond_wait(&waiting_buffer_requests[tid],
                &mutex_waiting_buffer_requests[tid]);
        }
        buffer_request_conditions[tid] = false;

        pthread_mutex_unlock(&mutex_waiting_buffer_requests[tid]);
    }
}

void ompt_buffer_add_target_event(ompt_record_t event) {
    bool tdata_complete;
    uint64_t pos;

    // The OMPT thread IDs start with 1 such that we will have 
    // to shift tid by 1 to get the right array elements.
    ompt_id_t tid = my_ompt_get_thread_data().value - 1;
    if (tracing) {

        // ensure we have valid buffer
        ompt_buffer_request(tid);

        event.thread_data.value = tid + 1;
        event.dev_task_id = 0; //FIXME

        pos = tdata_pos[tid];
        pthread_mutex_lock(&mutex_tdata);
        tdata[tid].event_buffer[pos] = event;
        pthread_mutex_unlock(&mutex_tdata);
        (tdata_pos[tid])++;
        tdata_complete = tdata_pos[tid] >= tdata[tid].buffer_size;

        if (tdata_complete) {
            ompt_buffer_flush(tid);
        }

    }
}



COINATIVELIBEXPORT
void ompt_target_start_tracing(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    OFFLOAD_OMPT_TRACE(3, "Start tracing\n");
    // initialize mutexes and condition variables
    // TODO: Is it really ok to init again and again if start
    // is call mulitple times? Better use static variable?
    for(int i=0; i<MAX_OMPT_THREADS; i++){
        pthread_cond_init(&waiting_buffer_requests[i], NULL);
        pthread_cond_init(&waiting_buffer_completes[i], NULL);
        pthread_mutex_init(&mutex_waiting_buffer_requests[i], NULL);
        pthread_mutex_init(&mutex_waiting_buffer_completes[i], NULL);
    }
    pthread_mutex_init(&mutex_tdata, NULL);

    ompt_buffer_request_events = (COIEVENT*) buffers[0];
    ompt_buffer_complete_events = (COIEVENT*) buffers[1];
    tdata_pos = (uint64_t*) buffers[2];

    tracing = true;
}

COINATIVELIBEXPORT
void ompt_target_stop_tracing(
uint32_t  buffer_count,
void**    buffers,
uint64_t* buffers_len,
void*     misc_data,
uint16_t  misc_data_len,
void*     return_data,
uint16_t  return_data_len
)
{
    tracing = false;

    OFFLOAD_OMPT_TRACE(3, "Stop tracing.\n");

    //pthread_mutex_lock(&mutex_tdata);
    std::map<uint64_t, ompt_thread_data_buffer_t>::iterator it;
    for(it = tdata.begin(); it != tdata.end(); it++){
        // FIXME: We delete the entry in thie flush routine.
        // This works although I always thought that the this
        // will invalidate the iterator.
        ompt_buffer_flush(it->first);

    }
  //  pthread_mutex_unlock(&mutex_tdata);
}

COINATIVELIBEXPORT
void ompt_target_pause_tracing(
uint32_t  buffer_count,
void**    buffers,
uint64_t* buffers_len,
void*     misc_data,
uint16_t  misc_data_len,
void*     return_data,
uint16_t  return_data_len
)
{
    OFFLOAD_OMPT_TRACE(3, "Pause tracing. Remaining buffers: (%d)\n",
        tdata.size());
    tracing = false;
}

COINATIVELIBEXPORT
void ompt_target_restart_tracing(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    OFFLOAD_OMPT_TRACE(3, "Restart tracing\n");
    tracing = true;
}

COINATIVELIBEXPORT
void ompt_signal_buffer_allocated(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    // get id of requesting thread and the host buffer size
    uint64_t tid = ((uint64_t*)misc_data)[0];
    uint64_t buffer_bytes = ((uint64_t*)misc_data)[1];
    //memcpy(&buffer_bytes, misc_data, sizeof(uint64_t));

    OFFLOAD_OMPT_TRACE(3, 
        "Received signal for complete buffer allocation (tid=%d, %d Bytes allocated)\n",
        tid, buffer_bytes);

    pthread_mutex_lock(&mutex_tdata);

    // store thread-specific allocated buffer and its size
    tdata[tid].buffer_size = buffer_bytes / sizeof(ompt_record_t);
    tdata[tid].event_buffer = (ompt_record_t*) buffers[0];

    pthread_mutex_unlock(&mutex_tdata);

    // signal that host memory has been allocated
    pthread_mutex_lock(&mutex_waiting_buffer_requests[tid]);
    buffer_request_conditions[tid] = true;
    pthread_cond_signal(&waiting_buffer_requests[tid]);
    pthread_mutex_unlock(&mutex_waiting_buffer_requests[tid]);
}

COINATIVELIBEXPORT
void ompt_signal_buffer_truncated(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    uint64_t tid = ((uint64_t*)misc_data)[0];

    OFFLOAD_OMPT_TRACE(3, 
        "Received signal for complete buffer truncation (tid=%d)\n", tid);

    pthread_mutex_lock(&mutex_waiting_buffer_completes[tid]);
    buffer_full_conditions[tid] = true;
    pthread_cond_signal(&waiting_buffer_completes[tid]);
    pthread_mutex_unlock(&mutex_waiting_buffer_completes[tid]);
}

/* Register OMPT callbacks on device */

/*******************************************************************
 * Function declaration
 *******************************************************************/

#define OMPT_FN_TYPE(fn) fn ## _t 
#define OMPT_FN_LOOKUP(lookup,fn) fn = (OMPT_FN_TYPE(fn)) lookup(#fn)
#define OMPT_FN_DECL(fn) OMPT_FN_TYPE(fn) fn

OMPT_FN_DECL(ompt_set_callback);
OMPT_FN_DECL(ompt_get_thread_data);

/*******************************************************************
 * required events 
 *******************************************************************/

TEST_NEW_THREAD_TYPE_CALLBACK(ompt_event_thread_begin)
TEST_THREAD_TYPE_CALLBACK(ompt_event_thread_end)
TEST_NEW_PARALLEL_CALLBACK(ompt_event_parallel_begin)
TEST_PARALLEL_CALLBACK(ompt_event_parallel_end)
TEST_NEW_TASK_CALLBACK(ompt_event_task_begin)
TEST_NEW_TASK_CALLBACK(ompt_event_task_end)
TEST_CONTROL_CALLBACK(ompt_event_control)
TEST_CALLBACK(ompt_event_runtime_shutdown)

/*******************************************************************
 * optional events
 *******************************************************************/

/* Blameshifting events */
TEST_THREAD_CALLBACK(ompt_event_idle_begin)
TEST_THREAD_CALLBACK(ompt_event_idle_end)
TEST_PARALLEL_CALLBACK(ompt_event_wait_barrier_begin)
TEST_PARALLEL_CALLBACK(ompt_event_wait_barrier_end)
TEST_PARALLEL_CALLBACK(ompt_event_wait_taskwait_begin)
TEST_PARALLEL_CALLBACK(ompt_event_wait_taskwait_end)
TEST_PARALLEL_CALLBACK(ompt_event_wait_taskgroup_begin)
TEST_PARALLEL_CALLBACK(ompt_event_wait_taskgroup_end)
TEST_WAIT_CALLBACK(ompt_event_release_lock)
TEST_WAIT_CALLBACK(ompt_event_release_nest_lock_last)
TEST_WAIT_CALLBACK(ompt_event_release_critical)
TEST_WAIT_CALLBACK(ompt_event_release_ordered)
TEST_WAIT_CALLBACK(ompt_event_release_atomic)

/* synchronous events */
TEST_NEW_IMPLICIT_TASK_CALLBACK(ompt_event_implicit_task_begin)
TEST_PARALLEL_CALLBACK(ompt_event_implicit_task_end)
TEST_PARALLEL_CALLBACK(ompt_event_initial_task_begin)
TEST_PARALLEL_CALLBACK(ompt_event_initial_task_end)
TEST_TASK_SWITCH_CALLBACK(ompt_event_task_switch)
TEST_WAIT_CALLBACK(ompt_event_init_lock)
TEST_WAIT_CALLBACK(ompt_event_init_nest_lock)
TEST_WAIT_CALLBACK(ompt_event_destroy_lock)
TEST_WAIT_CALLBACK(ompt_event_destroy_nest_lock)
TEST_NEW_WORKSHARE_CALLBACK(ompt_event_loop_begin)
TEST_PARALLEL_CALLBACK(ompt_event_loop_end)
TEST_NEW_WORKSHARE_CALLBACK(ompt_event_sections_begin)
TEST_PARALLEL_CALLBACK(ompt_event_sections_end)
TEST_NEW_WORKSHARE_CALLBACK(ompt_event_single_in_block_begin)
TEST_PARALLEL_CALLBACK(ompt_event_single_in_block_end)
TEST_PARALLEL_CALLBACK(ompt_event_single_others_begin)
TEST_PARALLEL_CALLBACK(ompt_event_single_others_end)
TEST_NEW_WORKSHARE_CALLBACK(ompt_event_workshare_begin)
TEST_PARALLEL_CALLBACK(ompt_event_workshare_end)
TEST_PARALLEL_CALLBACK(ompt_event_master_begin)
TEST_PARALLEL_CALLBACK(ompt_event_master_end)
TEST_PARALLEL_CALLBACK(ompt_event_barrier_begin)
TEST_PARALLEL_CALLBACK(ompt_event_barrier_end)
TEST_PARALLEL_CALLBACK(ompt_event_taskwait_begin)
TEST_PARALLEL_CALLBACK(ompt_event_taskwait_end)
TEST_PARALLEL_CALLBACK(ompt_event_taskgroup_begin)
TEST_PARALLEL_CALLBACK(ompt_event_taskgroup_end)
TEST_WAIT_CALLBACK(ompt_event_wait_lock)
TEST_WAIT_CALLBACK(ompt_event_acquired_lock)
TEST_WAIT_CALLBACK(ompt_event_wait_nest_lock)
TEST_WAIT_CALLBACK(ompt_event_acquired_nest_lock_first)
TEST_PARALLEL_CALLBACK(ompt_event_release_nest_lock_prev)
TEST_PARALLEL_CALLBACK(ompt_event_acquired_nest_lock_next)
TEST_WAIT_CALLBACK(ompt_event_wait_critical)
TEST_WAIT_CALLBACK(ompt_event_acquired_critical)
TEST_WAIT_CALLBACK(ompt_event_wait_ordered)
TEST_WAIT_CALLBACK(ompt_event_acquired_ordered)
TEST_WAIT_CALLBACK(ompt_event_wait_atomic)
TEST_WAIT_CALLBACK(ompt_event_acquired_atomic)
TEST_THREAD_CALLBACK(ompt_event_flush)

/*******************************************************************
 * Register the events
 *******************************************************************/

#define CHECK(EVENT) ompt_set_callback(EVENT, (ompt_callback_t) my_##EVENT); 

void ompt_initialize(ompt_function_lookup_t lookup, const char *runtime_version, unsigned int ompt_version) {
    OFFLOAD_OMPT_TRACE(3, "Initializing OMPT on device...\n");

    my_ompt_get_thread_data = (ompt_get_thread_data_t) lookup("ompt_get_thread_data");


    /* look up and bind OMPT API functions */

    OMPT_FN_LOOKUP(lookup,ompt_set_callback);
    OMPT_FN_LOOKUP(lookup,ompt_get_thread_data);

    /* required events */

    CHECK(ompt_event_parallel_begin);
    CHECK(ompt_event_parallel_end);
    CHECK(ompt_event_task_begin);
    CHECK(ompt_event_task_end);
    CHECK(ompt_event_thread_begin);
    CHECK(ompt_event_thread_end);
    CHECK(ompt_event_control);
    CHECK(ompt_event_runtime_shutdown);

    /* optional events, "blameshifting" */

    CHECK(ompt_event_idle_begin);
    CHECK(ompt_event_idle_end);
    CHECK(ompt_event_wait_barrier_begin);
    CHECK(ompt_event_wait_barrier_end);
    CHECK(ompt_event_wait_taskwait_begin);
    CHECK(ompt_event_wait_taskwait_end);
    CHECK(ompt_event_wait_taskgroup_begin);
    CHECK(ompt_event_wait_taskgroup_end);
    CHECK(ompt_event_release_lock);
    CHECK(ompt_event_release_nest_lock_last);
    CHECK(ompt_event_release_critical);
    CHECK(ompt_event_release_atomic);
    CHECK(ompt_event_release_ordered);

    /* optional events, synchronous */

    CHECK(ompt_event_implicit_task_begin);
    CHECK(ompt_event_implicit_task_end);
    CHECK(ompt_event_master_begin);
    CHECK(ompt_event_master_end);
    CHECK(ompt_event_barrier_begin);
    CHECK(ompt_event_barrier_end);
    CHECK(ompt_event_task_switch);
    CHECK(ompt_event_loop_begin);
    CHECK(ompt_event_loop_end);
    CHECK(ompt_event_sections_begin);
    CHECK(ompt_event_sections_end);
    CHECK(ompt_event_single_in_block_begin);
    CHECK(ompt_event_single_in_block_end);
    CHECK(ompt_event_single_others_begin);
    CHECK(ompt_event_single_others_end);
    CHECK(ompt_event_taskwait_begin);
    CHECK(ompt_event_taskwait_end);
    CHECK(ompt_event_taskgroup_begin);
    CHECK(ompt_event_taskgroup_end);
    CHECK(ompt_event_release_nest_lock_prev);
    CHECK(ompt_event_wait_lock);
    CHECK(ompt_event_wait_nest_lock);
    CHECK(ompt_event_wait_critical);
    CHECK(ompt_event_wait_atomic);
    CHECK(ompt_event_wait_ordered);
    CHECK(ompt_event_acquired_lock);
    CHECK(ompt_event_acquired_nest_lock_first);
    CHECK(ompt_event_acquired_nest_lock_next);
    CHECK(ompt_event_acquired_critical);
    CHECK(ompt_event_acquired_atomic);
    CHECK(ompt_event_acquired_ordered);
    CHECK(ompt_event_init_lock);
    CHECK(ompt_event_init_nest_lock);
    CHECK(ompt_event_destroy_lock);
    CHECK(ompt_event_destroy_nest_lock);
    CHECK(ompt_event_flush);
}

ompt_initialize_t
ompt_tool()
{
    OFFLOAD_OMPT_TRACE(3, "Register events on device\n");
    return ompt_initialize;
}
