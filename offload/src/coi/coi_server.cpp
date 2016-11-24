//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


// The COI interface on the target

#include "coi_server.h"

#include "../offload_target.h"
#include "../offload_timer.h"
#ifdef MYO_SUPPORT
#include "../offload_myo_target.h"      // for __offload_myoLibInit/Fini
#endif // MYO_SUPPORT

#if !defined(CPU_COUNT)
// if CPU_COUNT is not defined count number of CPUs manually 
static
int my_cpu_count(cpu_set_t const *cpu_set) 
{
    int res = 0;
    for (int i = 0; i < sizeof(cpu_set_t) / sizeof(__cpu_mask); ++i) {
        res += __builtin_popcountl(cpu_set->__bits[i]);
    }
    return res;
}
// Map CPU_COUNT to our function
#define CPU_COUNT(x) my_cpu_count(x)

#endif

COINATIVELIBEXPORT
void server_compute(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    OffloadDescriptor::offload(buffer_count, buffers,
                               misc_data, misc_data_len,
                               return_data, return_data_len);
}

COINATIVELIBEXPORT
void server_init(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    struct init_data {
        int  device_index;
        int  devices_total;
        int  console_level;
        int  offload_report_level;
    } *data = (struct init_data*) misc_data;

    // set device index and number of total devices
    mic_index = data->device_index;
    mic_engines_total = data->devices_total;

    // initialize trace level
    console_enabled = data->console_level;
    offload_report_level = data->offload_report_level;

    // return back the process id
    *((pid_t*) return_data) = getpid();
}

COINATIVELIBEXPORT
void server_var_table_size(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    struct Params {
        int64_t nelems;
        int64_t length;
    } *params;

    params = static_cast<Params*>(return_data);
    params->length = __offload_vars.table_size(params->nelems);
}

COINATIVELIBEXPORT
void server_var_table_copy(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    __offload_vars.table_copy(buffers[0], *static_cast<int64_t*>(misc_data));
}

COINATIVELIBEXPORT
void server_set_stream_affinity(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    static bool kmp_affinity_has_been_set = false;
    struct affinity_spec affinity_spec;
    cpu_set_t cpu_set;
    int num_threads_per_core;
            
    memcpy(&affinity_spec, misc_data, sizeof(affinity_spec));

    // Calculate threads per MIC core
    num_threads_per_core = affinity_spec.num_threads / affinity_spec.num_cores;

    // Set number of threads for the stream to be size of CPU mask
    sched_getaffinity(0, sizeof(cpu_set), &cpu_set);
    int cpu_count = CPU_COUNT(&cpu_set);
    omp_set_num_threads(cpu_count);

    // This causes OMP affinity initialization
    int num_threads = omp_get_max_threads();

    // Set affinity for each OMP thread
    #pragma omp parallel
    #pragma omp critical
    {
        int i, j, thread_no, omp_thread, ret;

        omp_thread = omp_get_thread_num();
        thread_no = -1;
        kmp_affinity_mask_t set;
        kmp_create_affinity_mask(&set);

        // We support compact and scatter only
        uint64_t temp_mask;
        uint64_t bit;
        for (i = 0; i < 16; i++)
        {
            temp_mask = affinity_spec.sink_mask[i];
            for (j = 0; j < 64 && temp_mask; j++)
            {
                bit = (uint64_t)(1) << j;
                if (bit & temp_mask)
                {
                    thread_no++;
                    temp_mask ^= bit;
                    int matched;

                    // Use affinity request received; default is compact
                    if (affinity_spec.affinity_type == affinity_scatter)
                    {
                        matched = thread_no ==
                                    (omp_thread * num_threads_per_core) % cpu_count +
                                    (omp_thread * num_threads_per_core) / cpu_count;
                    } else {
                        matched = (thread_no == omp_thread);
                    }
                    if (matched)
                    {
                        OFFLOAD_DEBUG_TRACE(2,
                            "OMP thread %02d matched available logical "
                            "MIC thread %02d, affinitized to MIC thread %d\n",
                            omp_thread, thread_no, i*64+j);
                        ret = kmp_set_affinity_mask_proc(i*64 + j, &set);
                        ret += kmp_set_affinity(&set);
                        if (ret != 0)
                        {
                            LIBOFFLOAD_ERROR(c_cannot_set_affinity);
                        }
                    }
                }
            }
        }
    }
}

#ifdef MYO_SUPPORT
// temporary workaround for blocking behavior of myoiLibInit/Fini calls
COINATIVELIBEXPORT
void server_myoinit(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    __offload_myoLibInit();
}

COINATIVELIBEXPORT
void server_myofini(
    uint32_t  buffer_count,
    void**    buffers,
    uint64_t* buffers_len,
    void*     misc_data,
    uint16_t  misc_data_len,
    void*     return_data,
    uint16_t  return_data_len
)
{
    __offload_myoLibFini();
}
#endif // MYO_SUPPORT
