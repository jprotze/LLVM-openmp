#include <stdio.h>
#include <stdlib.h>
#include <ompt.h>

/*
 * Macros to help generate test functions for each event
 */

#define TEST_THREAD_CALLBACK(EVENT) \
void my_##EVENT(ompt_thread_id_t thread_id) \
{ \
  printf("%d: %s: thread_id=%lu\n", omp_get_thread_num(), #EVENT, thread_id); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.thread = { \
    .thread_id = thread_id \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

// FIXME: thread end event cannot be transferred to the host, because the COI process
// will be already destroyed
#define TEST_THREAD_TYPE_CALLBACK(EVENT) \
void my_##EVENT(ompt_thread_type_t thread_type, ompt_thread_id_t thread_id) \
{ \
  const char * type_strings[] = {"initial", "worker", "other"}; \
  printf("%d: %s: thread_id=%lu thread_type=%d type_string='%s'\n", omp_get_thread_num(), #EVENT, thread_id, thread_type, type_strings[thread_type-1]); \
  fflush(stdout); \
  \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.thread_type = { \
    .thread_id = thread_id, \
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
  printf("%d: %s: waid_id=%lu\n", omp_get_thread_num(), #EVENT, waitid); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.wait = { \
    .wait_id = waitid \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_PARALLEL_CALLBACK(EVENT) \
void my_##EVENT ( \
ompt_parallel_id_t parallel_id,   /* id of parallel region       */ \
ompt_task_id_t task_id)           /* id for task                 */ \
{ \
  printf("%d: %s: parallel_id=%lu task_id=%lu\n", omp_get_thread_num(), #EVENT, parallel_id, task_id); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.parallel = { \
    .parallel_id = parallel_id, \
    .task_id = task_id \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_NEW_WORKSHARE_CALLBACK(EVENT) \
void my_##EVENT ( \
ompt_parallel_id_t parallel_id,   /* id of parallel region       */ \
ompt_task_id_t task_id,           /* id for task                 */ \
void *workshare_function)           /* ptr to outlined function  */ \
{ \
  printf("%d: %s: parallel_id=%lu task_id=%lu workshare_function=%p\n", omp_get_thread_num(), #EVENT, parallel_id, task_id, workshare_function); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.new_workshare = { \
    .parallel_id = parallel_id, \
    .task_id = task_id, \
    .workshare_function = workshare_function \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_NEW_PARALLEL_CALLBACK(EVENT) \
void my_##EVENT ( \
  ompt_task_id_t  parent_task_id,   /* tool data for parent task    */ \
  ompt_frame_t *parent_task_frame,  /* frame data of parent task    */ \
  ompt_parallel_id_t parallel_id,   /* id of parallel region        */ \
  uint32_t requested_team_size,     /* # threads requested for team */ \
  void *parallel_function)          /* outlined function            */ \
{ \
  printf("%d: %s: parent_task_id=%lu parent_task_frame=%p parallel_id=%lu team_size=%lu parallel_function=%p\n", omp_get_thread_num(), #EVENT, parent_task_id, parent_task_frame, parallel_id, parent_task_id, parallel_function); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.new_parallel = { \
    .parent_task_id = parent_task_id, \
    .parent_task_frame = parent_task_frame, \
    .parallel_id = parallel_id, \
    .requested_team_size = requested_team_size, \
    .parallel_function = parallel_function \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_TASK_CALLBACK(EVENT) \
void my_##EVENT ( \
ompt_task_id_t task_id)            /* tool data for task          */ \
{ \
  printf("%d: %s: task_id=%lu\n", omp_get_thread_num(), #EVENT, task_id); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.task = { \
    .task_id = task_id \
  }; \
  \
  ompt_buffer_add_target_event(event); \
} \

#define TEST_TASK_SWITCH_CALLBACK(EVENT) \
void my_##EVENT ( \
  ompt_task_id_t suspended_task_id, /* tool data for suspended task */ \
  ompt_task_id_t resumed_task_id)   /* tool data for resumed task   */ \
{ \
  printf("%d: %s: suspended_task_id=%lu resumed_task_id=%lu\n", omp_get_thread_num(), #EVENT, suspended_task_id, resumed_task_id); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.task_switch = { \
    .suspended_task_id = suspended_task_id, \
    .resumed_task_id = resumed_task_id \
  }; \
  \
  ompt_buffer_add_target_event(event); \
}

#define TEST_NEW_TASK_CALLBACK(EVENT) \
void my_##EVENT ( \
  ompt_task_id_t  parent_task_id,   /* tool data for parent task   */ \
  ompt_frame_t *parent_task_frame,  /* frame data of parent task   */ \
  ompt_task_id_t new_task_id,   /* id of parallel region       */ \
  void *new_task_function)          /* outlined function           */ \
{ \
  printf("%d: %s: parent_task_id=%lu parent_task_frame-=%p new_task_id=%lu parallel_function=%p\n", omp_get_thread_num(), #EVENT, parent_task_id, parent_task_frame, new_task_id, new_task_function); \
  fflush(stdout); \
  ompt_record_t event;\
  event.type = ##EVENT; \
  event.time = ompt_get_time(); \
  event.record.new_task = { \
    .parent_task_id = parent_task_id, \
    .parent_task_frame = parent_task_frame, \
    .new_task_id = new_task_id, \
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
  printf("%d: %s: command=%lu modifier=%lu\n", omp_get_thread_num(), #EVENT, command, modifier); \
  fflush(stdout); \
}


#define TEST_CALLBACK(EVENT) \
void my_##EVENT() \
{ \
  printf("%d: %s\n", omp_get_thread_num(), #EVENT); \
  fflush(stdout); \
}

#define TEST_NEW_TARGET_DATA_CALLBACK(event) \
void my_##event( \
  ompt_task_id_t task_id,            /* ID of parent task */ \
  ompt_target_device_id_t device_id, /* ID of the device */ \
  void *target_function              /* pointer to outlined function */ \
    ) \
{ \
  printf("%d: %s: task_id=%llu device_id=%llu target_function=%llu\n", omp_get_thread_num(), #event, task_id, device_id, target_function); \
  fflush(0); \
}

#define TEST_NEW_TARGET_TASK_CALLBACK(event) \
void my_##event( \
  ompt_task_id_t task_id,            /* ID of parent task */ \
  ompt_frame_t *parent_task_frame,   /* frame data for parent task */ \
  ompt_task_id_t target_task_id,   /* ID of created target task */ \
  ompt_target_device_id_t device_id, /* ID of the device */ \
  void *target_function              /* pointer to outlined function */ \
    ) \
{ \
  printf("%d: %s: task_id=%llu target_task_id=%llu device_id=%llu target_function=%llu\n", omp_get_thread_num(), #event, task_id, target_task_id, device_id, target_function); \
  fflush(0); \
} 


#define TEST_TARGET_CALLBACK(event) \
void my_##event( \
  ompt_task_id_t task_id,            /* ID of task */ \
  ompt_target_task_id_t target_task_id         /* ID of target* region */ \
    ) \
{ \
  printf("%d: %s: task_id=%llu target_task_id=%llu\n", omp_get_thread_num(), #event, task_id, target_task_id); \
  fflush(0); \
}

#define TEST_NEW_DATA_MAP_CALLBACK(event) \
void my_##event( \
  ompt_task_id_t task_id,            /* ID of task */ \
  ompt_target_device_id_t device_id, /* ID of the device */ \
  void *host_addr,                   /* host address of the data */ \
  void *device_addr,                 /* device address of the data */ \
  size_t bytes,                     /* amount of mapped bytes */ \
  uint32_t mapping_flags, \
  void *target_map_code \
    ) \
{ \
  printf("%d: %s: task_id=%llu device_id=%llu host_addr=%p device_addr=%p bytes=%llu mapping_flags=%llu\n", \
     omp_get_thread_num(), \
     #event, \
     task_id, \
     device_id, \
     host_addr, device_addr, \
     bytes, mapping_flags); \
  fflush(0); \
} 
