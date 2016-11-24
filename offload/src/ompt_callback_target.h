#include <stdio.h>
#include <stdlib.h>
#include <ompt.h>
#include <iostream>

/*
 * Macros to help generate test functions for each event
 */

#define TEST_THREAD_CALLBACK(EVENT) \
void my_##EVENT(ompt_thread_data_t thread_data) \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.thread = { \
    .thread_data = thread_data \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

// FIXME: thread end event cannot be transferred to the host, because the COI process
// will be already destroyed
#define TEST_THREAD_TYPE_CALLBACK(EVENT) \
void my_##EVENT(ompt_thread_type_t thread_type, ompt_thread_data_t thread_data) \
{ \
  const char * type_strings[] = {"initial", "worker", "other"}; \
  fflush(stdout); \
  \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.thread_type = { \
    .thread_data = thread_data, \
    .thread_type = thread_type \
  }; \
  if (##EVENT != ompt_event_thread_end) { \
    ompt_buffer_add_target_event(event); \
  } \
  \
} 

#define TEST_NEW_THREAD_TYPE_CALLBACK(EVENT) \
void my_##EVENT(ompt_thread_type_t thread_type, ompt_thread_data_t* thread_data) \
{ \
  const char * type_strings[] = {"initial", "worker", "other"}; \
  fflush(stdout); \
  \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.new_thread_type = { \
    .thread_data = *thread_data, \
    .thread_type = thread_type \
  }; \
  if (##EVENT != ompt_event_thread_end) { \
    ompt_buffer_add_target_event(event); \
  } \
  \
} 

#define TEST_WAIT_CALLBACK(EVENT) \
void my_##EVENT ( \
  ompt_wait_id_t waitid)            /* address of wait obj */ \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.wait = { \
    .wait_id = waitid \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_NEW_IMPLICIT_TASK_CALLBACK(EVENT) \
void my_##EVENT ( \
ompt_parallel_data_t parallel_data,   /* id of parallel region       */ \
ompt_task_data_t* task_data)           /* id for task                 */ \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.new_implicit_task = { \
    .parallel_data = parallel_data, \
    .task_data = *task_data \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_PARALLEL_CALLBACK(EVENT) \
void my_##EVENT ( \
ompt_parallel_data_t parallel_data,   /* id of parallel region       */ \
ompt_task_data_t task_data)           /* id for task                 */ \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.parallel = { \
    .parallel_data = parallel_data, \
    .task_data = task_data \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_NEW_WORKSHARE_CALLBACK(EVENT) \
void my_##EVENT ( \
ompt_parallel_data_t parallel_data,   /* id of parallel region       */ \
ompt_task_data_t task_data,           /* id for task                 */ \
void *workshare_function)           /* ptr to outlined function  */ \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.new_workshare = { \
    .parallel_data = parallel_data, \
    .task_data = task_data, \
    .workshare_function = workshare_function \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_NEW_PARALLEL_CALLBACK(EVENT) \
void my_##EVENT ( \
  ompt_task_data_t  parent_task_data,   /* tool data for parent task    */ \
  ompt_frame_t *parent_task_frame,  /* frame data of parent task    */ \
  ompt_parallel_data_t* parallel_data,   /* id of parallel region        */ \
  uint32_t requested_team_size,     /* # threads requested for team */ \
  void *parallel_function,          /* outlined function            */ \
  ompt_invoker_t invoker)            /* who invokes master task?     */\
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.new_parallel = { \
    .parent_task_data = parent_task_data, \
    .parent_task_frame = parent_task_frame, \
    .parallel_data = *parallel_data, \
    .requested_team_size = requested_team_size, \
    .parallel_function = parallel_function \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_TASK_CALLBACK(EVENT) \
void my_##EVENT ( \
ompt_task_data_t task_data)            /* tool data for task          */ \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.task = { \
    .task_data = task_data \
  }; \
  \
  ompt_buffer_add_target_event(event); \
} \

#define TEST_TASK_SWITCH_CALLBACK(EVENT) \
void my_##EVENT ( \
  ompt_task_data_t suspended_task_data, /* tool data for suspended task */ \
  ompt_task_data_t resumed_task_data)   /* tool data for resumed task   */ \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.task_switch = { \
    .suspended_task_data = suspended_task_data, \
    .resumed_task_data = resumed_task_data \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_NEW_TASK_CALLBACK(EVENT) \
void my_##EVENT ( \
  ompt_task_data_t  parent_task_data,   /* tool data for parent task   */ \
  ompt_frame_t *parent_task_frame,  /* frame data of parent task   */ \
  ompt_task_data_t *new_task_data,   /* id of parallel region       */ \
  void *new_task_function)          /* outlined function           */ \
{ \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = target_get_time(); \
  event.record.new_task = { \
    .parent_task_data = parent_task_data, \
    .parent_task_frame = parent_task_frame, \
    .new_task_data = new_task_data, \
    .new_task_function = new_task_function \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_CONTROL_CALLBACK(EVENT) \
void my_##EVENT( \
uint64_t command,                /* command of control call      */ \
uint64_t modifier)                /* modifier of control call     */ \
{ \
  fflush(stdout); \
}


#define TEST_CALLBACK(EVENT) \
void my_##EVENT() \
{ \
  fflush(stdout); \
}

#define TEST_NEW_TARGET_DATA_CALLBACK(event) \
void my_##event( \
  ompt_task_data_t task_data,            /* data of parent task */ \
  ompt_target_device_id_t device_id, /* ID of the device */ \
  void *target_function              /* pointer to outlined function */ \
    ) \
{ \
  fflush(0); \
}

#define TEST_NEW_TARGET_TASK_CALLBACK(event) \
void my_##event( \
  ompt_task_data_t task_data,            /* data of parent task */ \
  ompt_frame_t *parent_task_frame,   /* frame data for parent task */ \
  ompt_task_data_t target_task_data,   /* data of created target task */ \
  ompt_target_device_id_t device_id, /* ID of the device */ \
  void *target_function              /* pointer to outlined function */ \
    ) \
{ \
  fflush(0); \
} 


#define TEST_TARGET_CALLBACK(event) \
void my_##event( \
  ompt_task_data_t task_data,            /* data of task */ \
  ompt_target_task_data_t target_task_data         /* data of target* region */ \
    ) \
{ \
  fflush(0); \
}

#define TEST_NEW_DATA_MAP_CALLBACK(event) \
void my_##event( \
  ompt_task_data_t task_data,            /* data of task */ \
  ompt_target_device_id_t device_id, /* ID of the device */ \
  void *host_addr,                   /* host address of the data */ \
  void *device_addr,                 /* device address of the data */ \
  size_t bytes,                     /* amount of mapped bytes */ \
  uint32_t mapping_flags, \
  void *target_map_code \
    ) \
{ \
  fflush(0); \
} 
