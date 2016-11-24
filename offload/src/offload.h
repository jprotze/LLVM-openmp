//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


/*
 * Include file for Offload API.
 */

#ifndef OFFLOAD_H_INCLUDED
#define OFFLOAD_H_INCLUDED

#if defined(LINUX) || defined(FREEBSD)
#include <bits/functexcept.h>
#endif

#include <stddef.h>
#include <omp.h>

#ifdef TARGET_WINNT
// <stdint.h> is not compatible with Windows
typedef unsigned long long int  uint64_t;
#else
#include <stdint.h>
#endif  // TARGET_WINNT

#ifdef __cplusplus
extern "C" {
#endif

#define TARGET_ATTRIBUTE __declspec(target(mic))

/*
 *  The target architecture.
 */
typedef enum TARGET_TYPE {
    TARGET_NONE,    /* Undefine target */
    TARGET_HOST,    /* Host used as target */
    TARGET_MIC      /* MIC target */
} TARGET_TYPE;

/*
 *  The default target type.
 */
#define DEFAULT_TARGET_TYPE TARGET_MIC

/*
 *  The default target number.
 */
#define DEFAULT_TARGET_NUMBER 0

/*
 *  Offload status.
 */
typedef enum {
    OFFLOAD_SUCCESS = 0,
    OFFLOAD_DISABLED,               /* offload is disabled */
    OFFLOAD_UNAVAILABLE,            /* card is not available */
    OFFLOAD_OUT_OF_MEMORY,          /* not enough memory on device */
    OFFLOAD_PROCESS_DIED,           /* target process has died */
    OFFLOAD_ERROR                   /* unspecified error */
} _Offload_result;

typedef struct {
    _Offload_result result;         /* result, see above */
    int             device_number;  /* device number */
    size_t          data_sent;      /* number of bytes sent to the target */
    size_t          data_received;  /* number of bytes received by host */
} _Offload_status;

typedef uint64_t _Offload_stream;

#define OFFLOAD_STATUS_INIT(x) \
    ((x).result = OFFLOAD_DISABLED)

#define OFFLOAD_STATUS_INITIALIZER \
    { OFFLOAD_DISABLED, -1, 0, 0 }

/* Offload runtime interfaces */

extern int _Offload_number_of_devices(void);
extern int _Offload_get_device_number(void);
extern int _Offload_get_physical_device_number(void);

/* Offload stream runtime interfaces */

extern _Offload_stream _Offload_stream_create(
    int device,           // MIC device number
    int number_of_cpus    // Cores allocated to the stream
);

extern int _Offload_stream_destroy(
    int device,             // MIC device number
    _Offload_stream stream  // stream handle
);

extern int _Offload_stream_completed(
    int device,             // MIC device number
    _Offload_stream handle  // stream handle
);

/*
 * _Offload_shared_malloc/free are only supported when offload is enabled
 * else they are defined to malloc and free
*/
#ifdef __INTEL_OFFLOAD
extern void* _Offload_shared_malloc(size_t size);
extern void  _Offload_shared_free(void *ptr);
extern void* _Offload_shared_aligned_malloc(size_t size, size_t align);
extern void  _Offload_shared_aligned_free(void *ptr);
#else
#include <malloc.h>
#define _Offload_shared_malloc(size)                 malloc(size)
#define _Offload_shared_free(ptr)                    free(ptr);
#if defined(_WIN32)
#define _Offload_shared_aligned_malloc(size, align)  _aligned_malloc(size, align)
#define _Offload_shared_aligned_free(ptr)            _aligned_free(ptr);
#else
#define _Offload_shared_aligned_malloc(size, align)  memalign(align, size)
#define _Offload_shared_aligned_free(ptr)            free(ptr);
#endif
#endif


extern int _Offload_signaled(int index, void *signal);
extern void _Offload_report(int val);
extern int _Offload_find_associated_mic_memory(
   int           target,
   const void*   cpu_addr,
   void**        cpu_base_addr,
   uint64_t*     buf_length,
   void**        mic_addr,
   uint64_t*     mic_buf_start_offset,
   int*          is_static
);

/* OpenMP API */

extern void omp_set_default_device(int num);
extern int  omp_get_default_device(void);
extern int  omp_get_num_devices(void);

/* OpenMP API wrappers */

/* Set num_threads on target */
extern void omp_set_num_threads_target(
    TARGET_TYPE target_type,
    int target_number,
    int num_threads
);

/* Get max_threads from target */
extern int omp_get_max_threads_target(
    TARGET_TYPE target_type,
    int target_number
);

/* Get num_procs from target */
extern int omp_get_num_procs_target(
    TARGET_TYPE target_type,
    int target_number
);

/* Set dynamic on target */
extern void omp_set_dynamic_target(
    TARGET_TYPE target_type,
    int target_number,
    int num_threads
);

/* Get dynamic from target */
extern int omp_get_dynamic_target(
    TARGET_TYPE target_type,
    int target_number
);

/* Set nested on target */
extern void omp_set_nested_target(
    TARGET_TYPE target_type,
    int target_number,
    int nested
);

/* Get nested from target */
extern int omp_get_nested_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void omp_set_num_threads_target(
    TARGET_TYPE target_type,
    int target_number,
    int num_threads
);

extern int omp_get_max_threads_target(
    TARGET_TYPE target_type,
    int target_number
);

extern int omp_get_num_procs_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void omp_set_dynamic_target(
    TARGET_TYPE target_type,
    int target_number,
    int num_threads
);

extern int omp_get_dynamic_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void omp_set_nested_target(
    TARGET_TYPE target_type,
    int target_number,
    int num_threads
);

extern int omp_get_nested_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void omp_set_schedule_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_sched_t kind,
    int modifier
);

extern void omp_get_schedule_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_sched_t *kind,
    int *modifier
);

/* lock API functions */

typedef struct {
    omp_lock_t lock;
} omp_lock_target_t;

extern void omp_init_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_lock_target_t *lock
);

extern void omp_destroy_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_lock_target_t *lock
);

extern void omp_set_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_lock_target_t *lock
);

extern void omp_unset_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_lock_target_t *lock
);

extern int omp_test_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_lock_target_t *lock
);

/* nested lock API functions */

typedef struct {
    omp_nest_lock_t lock;
} omp_nest_lock_target_t;

extern void omp_init_nest_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_nest_lock_target_t *lock
);

extern void omp_destroy_nest_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_nest_lock_target_t *lock
);

extern void omp_set_nest_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_nest_lock_target_t *lock
);

extern void omp_unset_nest_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_nest_lock_target_t *lock
);

extern int omp_test_nest_lock_target(
    TARGET_TYPE target_type,
    int target_number,
    omp_nest_lock_target_t *lock
);

/* kmp API functions */

extern void kmp_set_stacksize_target(
    TARGET_TYPE target_type,
    int target_number,
    int size
);

extern int kmp_get_stacksize_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void kmp_set_stacksize_s_target(
    TARGET_TYPE target_type,
    int target_number,
    size_t size
);

extern size_t kmp_get_stacksize_s_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void kmp_set_blocktime_target(
    TARGET_TYPE target_type,
    int target_number,
    int time
);

extern int kmp_get_blocktime_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void kmp_set_library_serial_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void kmp_set_library_turnaround_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void kmp_set_library_throughput_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void kmp_set_library_target(
    TARGET_TYPE target_type,
    int target_number,
    int mode
);

extern int kmp_get_library_target(
    TARGET_TYPE target_type,
    int target_number
);

extern void kmp_set_defaults_target(
    TARGET_TYPE target_type,
    int target_number,
    char const *defaults
);

/* affinity API functions */

typedef struct {
    kmp_affinity_mask_t mask;
} kmp_affinity_mask_target_t;

extern void kmp_create_affinity_mask_target(
    TARGET_TYPE target_type,
    int target_number,
    kmp_affinity_mask_target_t *mask
);

extern void kmp_destroy_affinity_mask_target(
    TARGET_TYPE target_type,
    int target_number,
    kmp_affinity_mask_target_t *mask
);

extern int kmp_set_affinity_target(
    TARGET_TYPE target_type,
    int target_number,
    kmp_affinity_mask_target_t *mask
);

extern int kmp_get_affinity_target(
    TARGET_TYPE target_type,
    int target_number,
    kmp_affinity_mask_target_t *mask
);

extern int kmp_get_affinity_max_proc_target(
    TARGET_TYPE target_type,
    int target_number
);

extern int kmp_set_affinity_mask_proc_target(
    TARGET_TYPE target_type,
    int target_number,
    int proc,
    kmp_affinity_mask_target_t *mask
);

extern int kmp_unset_affinity_mask_proc_target(
    TARGET_TYPE target_type,
    int target_number,
    int proc,
    kmp_affinity_mask_target_t *mask
);

extern int kmp_get_affinity_mask_proc_target(
    TARGET_TYPE target_type,
    int target_number,
    int proc,
    kmp_affinity_mask_target_t *mask
);

#ifdef __cplusplus
} /* extern "C" */

/* Namespace for the shared_allocator. */
namespace __offload {
  /* This follows the specification for std::allocator. */
  /* Forward declaration of the class template. */
  template <typename T>
  class shared_allocator;

  /* Specialization for shared_allocator<void>. */
  template <>
  class shared_allocator<void> {
  public:
    typedef void       *pointer;
    typedef const void *const_pointer;
    typedef void        value_type;
    template <class U> struct rebind { typedef shared_allocator<U> other; };
  };

  /* Definition of shared_allocator<T>. */
  template <class T>
  class shared_allocator {
  public:
    typedef size_t     size_type;
    typedef ptrdiff_t  difference_type;
    typedef T         *pointer;
    typedef const T   *const_pointer;
    typedef T         &reference;
    typedef const T   &const_reference;
    typedef T          value_type;
    template <class U> struct rebind { typedef shared_allocator<U> other; };
    shared_allocator() throw() { }
    shared_allocator(const shared_allocator&) throw() { }
    template <class U> shared_allocator(const shared_allocator<U>&) throw() { }
    ~shared_allocator() throw() { }
    pointer address(reference x) const { return &x; }
    const_pointer address(const_reference x) const { return &x; }
    pointer allocate(
      size_type, shared_allocator<void>::const_pointer hint = 0);
    void deallocate(pointer p, size_type n);
    size_type max_size() const throw() {
      return size_type(-1)/sizeof(T);
    } /* max_size */
    void construct(pointer p, const T& arg) {
      ::new (p) T(arg);
    } /* construct */
    void destroy(pointer p) {
      p->~T();
    } /* destroy */
  };

  /* Definition for allocate. */
  template <class T>
  typename shared_allocator<T>::pointer
  shared_allocator<T>::allocate(shared_allocator<T>::size_type s,
                                shared_allocator<void>::const_pointer) {
    /* Allocate from shared memory. */
    void *ptr = _Offload_shared_malloc(s*sizeof(T));
#if (defined(_WIN32) || defined(_WIN64))   /* Windows */
        if (ptr == 0) throw std::bad_alloc();
#else
    if (ptr == 0) std::__throw_bad_alloc();
#endif
    return static_cast<pointer>(ptr);
  } /* allocate */

  template <class T>
  void shared_allocator<T>::deallocate(pointer p,
                                       shared_allocator<T>::size_type) {
    /* Free the shared memory. */
    _Offload_shared_free(p);
  } /* deallocate */

  template <typename _T1, typename _T2>
  inline bool operator==(const shared_allocator<_T1> &, 
                         const shared_allocator<_T2> &) throw() {
    return true;
  }  /* operator== */

  template <typename _T1, typename _T2>
  inline bool operator!=(const shared_allocator<_T1> &, 
                         const shared_allocator<_T2> &) throw() {
    return false;
  }  /* operator!= */
}  /* __offload */
#endif /* __cplusplus */

#endif /* OFFLOAD_H_INCLUDED */
