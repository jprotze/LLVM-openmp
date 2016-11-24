//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#include "offload_util.h"

#ifndef OFFLOAD_ORSL_H_INCLUDED
#define OFFLOAD_ORSL_H_INCLUDED

// ORSL interface
namespace ORSL {

DLL_LOCAL extern void init();

DLL_LOCAL extern bool reserve(int device);
DLL_LOCAL extern bool try_reserve(int device);
DLL_LOCAL extern void release(int device);

} // namespace ORSL

#endif // OFFLOAD_ORSL_H_INCLUDED
