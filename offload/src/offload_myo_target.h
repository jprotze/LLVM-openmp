//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#ifndef OFFLOAD_MYO_TARGET_H_INCLUDED
#define OFFLOAD_MYO_TARGET_H_INCLUDED


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
    SharedTableEntry *shared_table,
    FptrTableEntry *fptr_table
);

// Process shared variable, shared vtable and function and init routine tables.
// On the target side the contents of the tables are registered with MYO.
extern "C" void __offload_myoProcessTables(
    InitTableEntry* init_table,
    SharedTableEntry *shared_table,
    SharedTableEntry *shared_vtable,
    FptrTableEntry *fptr_table
);

extern "C" void __offload_myoAcquire(void);
extern "C" void __offload_myoRelease(void);

// Call the compiler-generated routines for initializing shared variables.
// This can only be done after shared memory allocation has been done.
extern void __offload_myo_shared_init_table_process(InitTableEntry* entry);

// temporary workaround for blocking behavior for myoiLibInit/Fini calls
extern "C" void __offload_myoLibInit();
extern "C" void __offload_myoLibFini();

#endif // OFFLOAD_MYO_TARGET_H_INCLUDED
