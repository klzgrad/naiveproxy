/*
 * Copyright (c) 2011 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
/*	CFBase.c
        Copyright (c) 1998-2011, Apple Inc. All rights reserved.
        Responsibility: Christopher Kane
*/

#ifndef PARTITION_ALLOC_THIRD_PARTY_APPLE_APSL_CFBASE_H_
#define PARTITION_ALLOC_THIRD_PARTY_APPLE_APSL_CFBASE_H_

#include "CFRuntime.h"

struct ChromeCFAllocatorLions {
  ChromeCFRuntimeBase _base;
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
  size_t (*size)(
      struct _malloc_zone_t* zone,
      const void* ptr); /* returns the size of a block or 0 if not in this zone;
                           must be fast, especially for negative answers */
  void* (*malloc)(struct _malloc_zone_t* zone, size_t size);
  void* (*calloc)(
      struct _malloc_zone_t* zone,
      size_t num_items,
      size_t size); /* same as malloc, but block returned is set to zero */
  void* (*valloc)(struct _malloc_zone_t* zone,
                  size_t size); /* same as malloc, but block returned is set to
                                   zero and is guaranteed to be page aligned */
  void (*free)(struct _malloc_zone_t* zone, void* ptr);
  void* (*realloc)(struct _malloc_zone_t* zone, void* ptr, size_t size);
  void (*destroy)(struct _malloc_zone_t*
                      zone); /* zone is destroyed and all memory reclaimed */
  const char* zone_name;

  /* Optional batch callbacks; these may be NULL */
  unsigned (*batch_malloc)(
      struct _malloc_zone_t* zone,
      size_t size,
      void** results,
      unsigned
          num_requested); /* given a size, returns pointers capable of holding
                             that size; returns the number of pointers allocated
                             (maybe 0 or less than num_requested) */
  void (*batch_free)(
      struct _malloc_zone_t* zone,
      void** to_be_freed,
      unsigned num_to_be_freed); /* frees all the pointers in to_be_freed; note
                                    that to_be_freed may be overwritten during
                                    the process */

  struct malloc_introspection_t* introspect;
  unsigned version;

  /* aligned memory allocation. The callback may be NULL. */
  void* (*memalign)(struct _malloc_zone_t* zone, size_t alignment, size_t size);

  /* free a pointer known to be in zone and known to have the given size. The
   * callback may be NULL. */
  void (*free_definite_size)(struct _malloc_zone_t* zone,
                             void* ptr,
                             size_t size);
#endif
  CFAllocatorRef _allocator;
  CFAllocatorContext _context;
};

#endif  // PARTITION_ALLOC_THIRD_PARTY_APPLE_APSL_CFBASE_H_
