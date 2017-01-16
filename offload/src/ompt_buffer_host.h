#ifndef OMPT_BUFFER_HOST_H_INCLUDED
#define OMPT_BUFFER_HOST_H_INCLUDED

#include "ompt_common.h"

#include <map>
#include <pthread.h>
#include <assert.h>

enum func_handle {
    c_ompt_func_start_tracing = 0,
    c_ompt_func_stop_tracing,
    c_ompt_func_pause_tracing,
    c_ompt_func_restart_tracing,
    c_ompt_func_signal_buffer_allocated,
    c_ompt_func_signal_buffer_truncated,
    c_ompt_func_target_get_time,
    c_ompt_funcs_total,
    c_ompt_funcs_invalid
};

static const char *ompt_func_names[] = {
    "ompt_target_start_tracing", "ompt_target_stop_tracing",
    "ompt_target_pause_tracing", "ompt_target_restart_tracing",
    "ompt_signal_buffer_allocated",   "ompt_signal_buffer_truncated",
    "ompt_target_get_time"};

typedef struct {
    COIBUFFER buffer;
    uint64_t host_size;
    ompt_record_t* host_ptr;
} thread_data_t;

struct Tracer {
    Tracer();
    virtual ~Tracer();

  private:
    COIFUNCTION ompt_funcs[c_ompt_funcs_total];
    COIPROCESS m_proc;
    int m_device_id;
    int m_tracing;
    int m_paused;
    int m_funcs_inited;

    pthread_t        m_signal_requested_thread;
    pthread_t        m_signal_truncated_thread;
    pthread_mutex_t  m_signal_req_thread_mutex;
    pthread_mutex_t  m_signal_tru_thread_mutex;
    pthread_cond_t   m_signal_req_thread_cond;
    pthread_cond_t   m_signal_tru_thread_cond;
    pthread_attr_t   m_signal_thread_attr;

    bool m_signal_req_thread_busy;
    bool m_signal_tru_thread_busy;

    ompt_target_buffer_request_callback_t request_callback;
    ompt_target_buffer_complete_callback_t complete_callback;

    // Map holding the OMPT events. The key is the thread ID,
    // the value holds the OMPT records, the COIBUFFER handle
    // and the size.
    // Note: In general locking the data structure is required.
    // However, since we lock on the device side, the locking on
    // the host is quaranteed implicitly.
    std::map<uint64_t, thread_data_t> tdata;

    uint64_t m_buffer_pos[MAX_OMPT_THREADS];
    COIEVENT m_request_events[MAX_OMPT_THREADS];
    COIEVENT m_full_events[MAX_OMPT_THREADS];
    COIBUFFER m_pos_buffer;
    COIBUFFER m_request_event_buffer;
    COIBUFFER m_full_event_buffer;

    /**
     * Simplifies pipeline creation.
     */
    COIPIPELINE get_pipeline();

    /**
     * Calls COI::ProcessGetFunctionHandles
     */
    void init_functions();

    /**
     * Pull OMPT buffer entries from device.
     */
    void read_buffer(COIBUFFER buffer, void *target_host, uint64_t bytes, uint64_t offset = 0);


  public:
    inline void set_coi_process(COIPROCESS proc) { m_proc = proc; }

    inline void set_device_id(int device_id) { m_device_id = device_id; }

    inline void set_callbacks(ompt_target_buffer_request_callback_t request,
                              ompt_target_buffer_complete_callback_t complete) {
        request_callback = request;
        complete_callback = complete;
    }

    inline int tracing() { return m_tracing; }

    /**
     * Helper functions
     */
    static void *signal_requested_helper(void* data);
    static void *signal_truncated_helper(void* data);

    /**
     * Get target time
     */
    uint64_t get_time();

    /**
     * Signals the device that the buffer entries have been
     * transferred to the host (after a complete callback) or
     * the host memory for a thread-specific OMPT event has
     * been registered (after a buffer request callback).
     */
    void *signal_requested();
    void *signal_truncated();

    /**
     * Starts the actual tracing of OMPT events on the device.
     * We distinguish two cases:
     * 1) If the tracer is started the first time or after it was stopped, then
     *    it requests host memory in order to transfer buffered entries between
     *    host and device.
     * 2) If the tracer was paused and tracing ist resumed, then the old buffers
     *    are used. The previous collected entries are preserved.
     * Note: Calling start() is not thread-safe.
     */
    void start();

    /**
     * Stops the tracing of OMPT events on the device and additionally
     * flushes all buffered entries.
     * @param final Specifies if this is the final stop (requiered for some
     *              cleanup). Default is false.
     * Note: Calling stop() is not thread-safe.
     */
    void stop(bool final=false);

    /**
     * Pauses the tracing of OMPT events on the device. The buffers
     * on the device are not flushed. They will be filled further
     * as soon as tracing is restarted.
     * Note: Calling pause() is not thread-safe.
     */
    void pause();

};
#endif
