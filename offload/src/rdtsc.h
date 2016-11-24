//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef RDTSC_H_INCLUDED
#define RDTSC_H_INCLUDED

#include <stdint.h>

static uint64_t _rdtsc()
{
  uint32_t eax, edx;
  asm volatile ("rdtsc" : "=a" (eax), "=d" (edx));
  return ((uint64_t)edx << 32) | eax;
}

#endif // RDTSC_H_INCLUDED
