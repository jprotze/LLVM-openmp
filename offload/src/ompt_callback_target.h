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
  printf("(MIC) %d: %s: thread_data=%lu\n", omp_get_thread_num(), #EVENT, thread_data.value); \
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
  printf("(MIC) %d: %s: thread_data=%d thread_type=%d type_string='%s'\n", omp_get_thread_num(), #EVENT, thread_data.value, thread_type, type_strings[thread_type-1]); \
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
  printf("(MIC) %d: %s: thread_data=%d thread_type=%d type_string='%s'\n", omp_get_thread_num(), #EVENT, thread_data->value, thread_type, type_strings[thread_type-1]); \
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
  printf("(MIC) %d: %s: wait_id=%lu\n", omp_get_thread_num(), #EVENT, waitid); \
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
  printf("(MIC) %d: %s: parallel_data=%lu task_data=%d\n", omp_get_thread_num(), #EVENT, parallel_data.value, task_data->value); \
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
  printf("(MIC) %d: %s: parallel_data=%lu task_data=%d\n", omp_get_thread_num(), #EVENT, parallel_data.value, task_data.value); \
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
  printf("(MIC) %d: %s: parallel_data=%lu task_data=%lu workshare_function=%p\n", omp_get_thread_num(), #EVENT, parallel_data.value, task_data, workshare_function); \
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
  printf("(MIC) %d: %s: parent_task_data=%lu parent_task_frame=%p parallel_data=%d team_size=%lu parallel_function=%p\n", omp_get_thread_num(), #EVENT, parent_task_data, parent_task_frame, parallel_data->value, requested_team_size, parallel_function); \
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
  printf("(MIC) %d: %s: task_data=%lu\n", omp_get_thread_num(), #EVENT, task_data); \
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
  printf("(MIC) %d: %s: suspended_task_data=%lu resumed_task_data=%lu\n", omp_get_thread_num(), #EVENT, suspended_task_data, resumed_task_data); \
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
  printf("(MIC) %d: %s: parent_task_data=%lu parent_task_frame-=%p new_task_data=%d parallel_function=%p\n", omp_get_thread_num(), #EVENT, parent_task_data, parent_task_frame, new_task_data->value, new_task_function); \
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
  printf("(MIC) %d: %s: command=%lu modifier=%lu\n", omp_get_thread_num(), #EVENT, command, modifier); \
  fflush(stdout); \
}


#define TEST_CALLBACK(EVENT) \
void my_##EVENT() \
{ \
  printf("(MIC) %d: %s\n", omp_get_thread_num(), #EVENT); \
  fflush(stdout); \
}

#define TEST_NEW_TARGET_DATA_CALLBACK(event) \
void my_##event( \
  ompt_task_data_t task_data,            /* data of parent task */ \
  ompt_target_device_id_t device_id, /* ID of the device */ \
  void *target_function              /* pointer to outlined function */ \
    ) \
{ \
  printf("(MIC) %d: %s: task_data=%llu device_id=%llu target_function=%llu\n", omp_get_thread_num(), #event, task_data, device_id, target_function); \
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
  printf("(MIC) %d: %s: task_data=%llu target_task_data=%llu device_id=%llu target_function=%llu\n", omp_get_thread_num(), #event, task_data, target_task_data, device_id, target_function); \
  fflush(0); \
} 


#define TEST_TARGET_CALLBACK(event) \
void my_##event( \
  ompt_task_data_t task_data,            /* data of task */ \
  ompt_target_task_data_t target_task_data         /* data of target* region */ \
    ) \
{ \
  printf("(MIC) %d: %s: task_data=%llu target_task_data=%llu\n", omp_get_thread_num(), #event, task_data, target_task_data); \
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
  printf("(MIC) %d: %s: task_data=%llu device_id=%llu host_addr=%p device_addr=%p bytes=%llu mapping_flags=%llu\n", \
     omp_get_thread_num(), \
     #event, \
     task_data, \
     device_id, \
     host_addr, device_addr, \
     bytes, mapping_flags); \
  fflush(0); \
} 
