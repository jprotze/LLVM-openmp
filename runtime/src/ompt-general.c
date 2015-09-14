/*****************************************************************************
 * system include files
 ****************************************************************************/

#include <assert.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



/*****************************************************************************
 * ompt include files
 ****************************************************************************/

#include "kmp_config.h"
#include "ompt-internal.h"
#include "ompt-specific.c"



/*****************************************************************************
 * macros
 ****************************************************************************/

#define ompt_get_callback_success 1
#define ompt_get_callback_failure 0

#define no_tool_present 0

#define OMPT_API_ROUTINE static



/*****************************************************************************
 * types
 ****************************************************************************/

typedef struct {
    const char *state_name;
    ompt_state_t  state_id;
} ompt_state_info_t;



/*****************************************************************************
 * global variables
 ****************************************************************************/

ompt_status_t ompt_status = ompt_status_ready;


ompt_state_info_t ompt_state_info[] = {
#define ompt_state_macro(state, code) { # state, state },
    FOREACH_OMPT_STATE(ompt_state_macro)
#undef ompt_state_macro
};


ompt_callbacks_t ompt_callbacks;

// function pointers for target tracing, these will be overwritten
// by ompt_target_initialize
ompt_recording_start_t __ompt_recording_start;
ompt_recording_stop_t __ompt_recording_stop;


/*****************************************************************************
 * forward declarations
 ****************************************************************************/

static ompt_interface_fn_t ompt_fn_lookup(const char *s);
OMPT_API_ROUTINE ompt_task_id_t ompt_get_task_id(int depth);


/*****************************************************************************
 * state
 ****************************************************************************/

OMPT_API_ROUTINE int ompt_enumerate_state(int current_state, int *next_state,
                                          const char **next_state_name)
{
    const static int len = sizeof(ompt_state_info) / sizeof(ompt_state_info_t);
    int i = 0;

    for (i = 0; i < len - 1; i++) {
        if (ompt_state_info[i].state_id == current_state) {
            *next_state = ompt_state_info[i+1].state_id;
            *next_state_name = ompt_state_info[i+1].state_name;
            return 1;
        }
    }

    return 0;
}



/*****************************************************************************
 * callbacks
 ****************************************************************************/

OMPT_API_ROUTINE int ompt_set_callback(ompt_event_t evid, ompt_callback_t cb)
{
    switch (evid) {

#define ompt_event_macro(event_name, callback_type, event_id)                  \
    case event_name:                                                           \
        if (ompt_event_implementation_status(event_name)) {                    \
            ompt_callbacks.ompt_callback(event_name) = (callback_type) cb;     \
        }                                                                      \
        return ompt_event_implementation_status(event_name);

    FOREACH_OMPT_EVENT(ompt_event_macro)

#undef ompt_event_macro

    default: return ompt_set_result_registration_error;
    }
}


OMPT_API_ROUTINE int ompt_get_callback(ompt_event_t evid, ompt_callback_t *cb)
{
    switch (evid) {

#define ompt_event_macro(event_name, callback_type, event_id)                  \
    case event_name:                                                           \
        if (ompt_event_implementation_status(event_name)) {                    \
            ompt_callback_t mycb =                                             \
                (ompt_callback_t) ompt_callbacks.ompt_callback(event_name);    \
            if (mycb) {                                                        \
                *cb = mycb;                                                    \
                return ompt_get_callback_success;                              \
            }                                                                  \
        }                                                                      \
        return ompt_get_callback_failure;

    FOREACH_OMPT_EVENT(ompt_event_macro)

#undef ompt_event_macro

    default: return ompt_get_callback_failure;
    }
}



/*****************************************************************************
 * intialization/finalization
 ****************************************************************************/

_OMP_EXTERN __attribute__ (( weak ))
int ompt_initialize(ompt_function_lookup_t ompt_fn_lookup, const char *version,
                    unsigned int ompt_version)
{
    return no_tool_present;
}

enum tool_setting_e {
    omp_tool_error,
    omp_tool_unset,
    omp_tool_disabled,
    omp_tool_enabled
};

void ompt_init()
{
    static int ompt_initialized = 0;

    if (ompt_initialized) return;

    const char *ompt_env_var = getenv("OMP_TOOL");
    tool_setting_e tool_setting = omp_tool_error;

    if (!ompt_env_var  || !strcmp(ompt_env_var, ""))
        tool_setting = omp_tool_unset;
    else if (!strcmp(ompt_env_var, "disabled"))
        tool_setting = omp_tool_disabled;
    else if (!strcmp(ompt_env_var, "enabled"))
        tool_setting = omp_tool_enabled;

    switch(tool_setting) {
    case omp_tool_disabled:
        ompt_status = ompt_status_disabled;
        break;

    case omp_tool_unset:
    case omp_tool_enabled:
    {
        const char *runtime_version = __ompt_get_runtime_version_internal();
        int ompt_init_val =
            ompt_initialize(ompt_fn_lookup, runtime_version, OMPT_VERSION);

        if (ompt_init_val) {
            ompt_status = ompt_status_track_callback;
            __ompt_init_internal();
        }
        break;
    }

    case omp_tool_error:
        fprintf(stderr,
            "Warning: OMP_TOOL has invalid value \"%s\".\n"
            "  legal values are (NULL,\"\",\"disabled\","
            "\"enabled\").\n", ompt_env_var);
        break;
    }

    // target tracing function pointers
    __ompt_recording_start = &__ompt_recording_start_internal;
    __ompt_recording_stop = &__ompt_recording_stop_internal;

    ompt_initialized = 1;
}

_OMP_EXTERN void
ompt_target_initialize(ompt_get_task_id_t *ompt_get_task_id_p,
                       ompt_enabled_t *ompt_enabled_p,
                       ompt_get_target_callback_t *ompt_get_target_callback_p,
                       ompt_recording_start_t ompt_recording_start_p,
                       ompt_recording_stop_t ompt_recording_stop_p)
{
    static int ompt_target_init = 0;

    // may only be called once
    assert(!ompt_target_init);
    ompt_target_init = 1;

    // initialize the runtime (initial thread and call to ompt_initialize)
    __kmp_serial_initialize();

    // set pointers
    *ompt_get_task_id_p = &ompt_get_task_id;
    *ompt_enabled_p = &__ompt_enabled;
    *ompt_get_target_callback_p = &__ompt_get_target_callback;

    // override empty tracing functions
    __ompt_recording_start = ompt_recording_start_p;
    __ompt_recording_stop = ompt_recording_stop_p;
}

void ompt_fini()
{
    if (ompt_status == ompt_status_track_callback) {
        if (ompt_callbacks.ompt_callback(ompt_event_runtime_shutdown)) {
            ompt_callbacks.ompt_callback(ompt_event_runtime_shutdown)();
        }
    }

    ompt_status = ompt_status_disabled;
}



/*****************************************************************************
 * parallel regions
 ****************************************************************************/

OMPT_API_ROUTINE ompt_parallel_id_t ompt_get_parallel_id(int ancestor_level)
{
    return __ompt_get_parallel_id_internal(ancestor_level);
}


OMPT_API_ROUTINE int ompt_get_parallel_team_size(int ancestor_level)
{
    return __ompt_get_parallel_team_size_internal(ancestor_level);
}


OMPT_API_ROUTINE void *ompt_get_parallel_function(int ancestor_level)
{
    return __ompt_get_parallel_function_internal(ancestor_level);
}


OMPT_API_ROUTINE ompt_state_t ompt_get_state(ompt_wait_id_t *ompt_wait_id)
{
    ompt_state_t thread_state = __ompt_get_state_internal(ompt_wait_id);

    if (thread_state == ompt_state_undefined) {
        thread_state = ompt_state_work_serial;
    }

    return thread_state;
}



/*****************************************************************************
 * threads
 ****************************************************************************/


OMPT_API_ROUTINE void *ompt_get_idle_frame()
{
    return __ompt_get_idle_frame_internal();
}



/*****************************************************************************
 * tasks
 ****************************************************************************/


OMPT_API_ROUTINE ompt_thread_id_t ompt_get_thread_id(void)
{
    return __ompt_get_thread_id_internal();
}

OMPT_API_ROUTINE ompt_task_id_t ompt_get_task_id(int depth)
{
    return __ompt_get_task_id_internal(depth);
}


OMPT_API_ROUTINE ompt_frame_t *ompt_get_task_frame(int depth)
{
    return __ompt_get_task_frame_internal(depth);
}


OMPT_API_ROUTINE void *ompt_get_task_function(int depth)
{
    return __ompt_get_task_function_internal(depth);
}



/*****************************************************************************
 * target
 ****************************************************************************/

OMPT_API_ROUTINE int
ompt_recording_start(int device_id,
                        ompt_target_buffer_request_callback_t request,
                        ompt_target_buffer_complete_callback_t complete)
{
    return __ompt_recording_start(device_id, request, complete);
}


OMPT_API_ROUTINE int ompt_recording_stop(int device_id)
{
    return __ompt_recording_stop(device_id);
}



/*****************************************************************************
 * placeholders
 ****************************************************************************/

// Don't define this as static. The loader may choose to eliminate the symbol
// even though it is needed by tools.  
#define OMPT_API_PLACEHOLDER 

// Ensure that placeholders don't have mangled names in the symbol table.
#ifdef __cplusplus
extern "C" {
#endif


OMPT_API_PLACEHOLDER void ompt_idle(void)  
{
    // This function is a placeholder used to represent the calling context of
    // idle OpenMP worker threads. It is not meant to be invoked.
    assert(0);
}


OMPT_API_PLACEHOLDER void ompt_overhead(void)
{
    // This function is a placeholder used to represent the OpenMP context of
    // threads working in the OpenMP runtime.  It is not meant to be invoked.
    assert(0);
}


OMPT_API_PLACEHOLDER void ompt_barrier_wait(void)
{
    // This function is a placeholder used to represent the OpenMP context of
    // threads waiting for a barrier in the OpenMP runtime. It is not meant
    // to be invoked.
    assert(0);
}


OMPT_API_PLACEHOLDER void ompt_task_wait(void)
{
    // This function is a placeholder used to represent the OpenMP context of
    // threads waiting for a task in the OpenMP runtime. It is not meant
    // to be invoked.
    assert(0);
}


OMPT_API_PLACEHOLDER void ompt_mutex_wait(void)
{
    // This function is a placeholder used to represent the OpenMP context of
    // threads waiting for a mutex in the OpenMP runtime. It is not meant
    // to be invoked.
    assert(0);
}

#ifdef __cplusplus
};
#endif


/*****************************************************************************
 * compatability
 ****************************************************************************/

OMPT_API_ROUTINE int ompt_get_ompt_version()
{
    return OMPT_VERSION;
}



/*****************************************************************************
 * application-facing API
 ****************************************************************************/


/*----------------------------------------------------------------------------
 | control
 ---------------------------------------------------------------------------*/

_OMP_EXTERN void ompt_control(uint64_t command, uint64_t modifier)
{
    if (ompt_status == ompt_status_track_callback &&
        ompt_callbacks.ompt_callback(ompt_event_control)) {
        ompt_callbacks.ompt_callback(ompt_event_control)(command, modifier);
    }
}



/*****************************************************************************
 * API inquiry for tool
 ****************************************************************************/

static ompt_interface_fn_t ompt_fn_lookup(const char *s)
{

#define ompt_interface_fn(fn) \
    if (strcmp(s, #fn) == 0) return (ompt_interface_fn_t) fn;

    FOREACH_OMPT_INQUIRY_FN(ompt_interface_fn)

    FOREACH_OMPT_PLACEHOLDER_FN(ompt_interface_fn)

    return (ompt_interface_fn_t) 0;
}
