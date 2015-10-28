#ifndef OMPT_BUFFER_HOST_H_INCLUDED
#define OMPT_BUFFER_HOST_H_INCLUDED

enum {
    c_ompt_func_start_tracing = 0,
    c_ompt_func_stop_tracing,
    c_ompt_func_restart_tracing,
    c_ompt_func_signal_buffer_allocated,
    c_ompt_func_signal_buffer_truncated,
    c_ompt_funcs_total
};

static const char *ompt_func_names[] = {
    "ompt_target_start_tracing", "ompt_target_stop_tracing",
    "ompt_target_restart_tracing", "ompt_signal_buffer_allocated",
    "ompt_signal_buffer_truncated"};

typedef struct {
    ompt_thread_id_t tid;
    int device_id;
} ompt_buffer_info_t;

typedef struct {
    COIBUFFER buffer;
    uint64_t host_size;
} thread_buffer_t;

struct Tracer {
    Tracer() : m_proc(NULL), m_device_id(-1), m_tracing(0), m_paused(0) {}

  private:
    // TODO: cleanup
    COIFUNCTION ompt_funcs[c_ompt_funcs_total];
    ompt_target_buffer_request_callback_t request_callback;
    ompt_target_buffer_complete_callback_t complete_callback;
    COIBUFFER request_event_buffer;
    COIBUFFER full_event_buffer;
    ompt_record_t *host_ptrs[240];
    COIBUFFER buffer_pos;
    uint64_t pos[240];
    COIEVENT request_events[240];
    COIEVENT full_events[240];
    thread_buffer_t tbuf[240];

    COIPROCESS m_proc;
    int m_device_id;
    int m_tracing;
    int m_paused;

    /**
     * Simplifies pipeline creation.
     */
    COIPIPELINE create_pipeline();

    /**
     * Pull OMPT buffer entries from device.
     */
    void pull_buffer(COIBUFFER buffer, void *target_host, size_t bytes);

    /**
     * Register COI event and transfer it to the device such that the
     * device can signal it.
     */
    void register_event(COIBUFFER buffer, uint64_t offset, COIEVENT *event);

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
    static void *signal_buffer_allocated_helper(void *data);
    static void *signal_buffer_truncated_helper(void *data);
    static void notification_callback_helper(COI_NOTIFICATIONS in_type,
                                             COIPROCESS in_Process,
                                             COIEVENT in_Event,
                                             const void *in_UserData);

    /**
     * Signals the device that host memory for a thread-specific
     * OMPT event buffer has been registered.
     */
    void *signal_buffer_allocated(int tid);

    /**
     * Signals the device that the buffer entries have been
     * transferred on the host.
     */
    void *signal_buffer_truncated();

    /**
    * Callback function invoked for each COI notification
    */
    void notification_callback(COI_NOTIFICATIONS in_type, COIPROCESS in_Process,
                               COIEVENT in_Event, const void *in_UserData);

    /**
     * Starts the actual tracing of OMPT events on the device.
     * We distinguish two cases:
     * 1) If the tracer is started the first time or after it was stopped, then
     *    it requests host memory in order to transfer buffered entries between
     *    host and device.
     * 2) If the tracer was paused and tracing ist resumed, then the old buffers
     *    are used. The previous collected entries are preserved.
     */
    void start();

    /**
     * Stops the tracing of OMPT events on the device and additionally
     * flushes all buffered entries.
     */
    void stop();

    /**
     * Pauses the tracing of OMPT events on the device. The buffers
     * on the device are not flushed. They will be filled further
     * as soon as tracing is restarted.
     */
    void pause();

    /**
     * Pulls explicitly all buffered OMPT events from the device.
     */
    void flush();
};
#endif
