//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#ifndef OFFLOAD_MYO_HOST_H_INCLUDED
#define OFFLOAD_MYO_HOST_H_INCLUDED

#include <myotypes.h>
#include <myoimpl.h>
#include <myo.h>

#include "offload.h"
// undefine the following since offload.h defines them to malloc and free if __INTEL_OFFLOAD 
// is not defined which is the case when building the offload library
#undef _Offload_shared_malloc
#undef _Offload_shared_free
#undef _Offload_shared_aligned_malloc
#undef _Offload_shared_aligned_free
#include "offload_table.h"

// This function retained for compatibility with 15.0
extern "C" void __offload_myoRegisterTables(
    InitTableEntry *init_table,
    SharedTableEntry *shared_table,
    FptrTableEntry *fptr_table
);

// Process shared variable, shared vtable and function and init routine tables.
// In .dlls/.sos these will be collected together.
// In the main program, all collected tables will be processed.
extern "C" bool __offload_myoProcessTables(
    const void* image,
    MYOInitTableList::Node *init_table,
    MYOVarTableList::Node  *shared_table,
    MYOVarTableList::Node  *shared_vtable,
    MYOFuncTableList::Node *fptr_table
);

extern void __offload_myoFini(void);
extern bool __offload_myo_init_is_deferred(const void *image);

#endif // OFFLOAD_MYO_HOST_H_INCLUDED
