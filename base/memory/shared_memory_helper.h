// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_SHARED_MEMORY_HELPER_H_
#define BASE_MEMORY_SHARED_MEMORY_HELPER_H_

#include "base/memory/shared_memory.h"

#include <fcntl.h>

namespace base {

#if !defined(OS_ANDROID)
// Makes a temporary file, fdopens it, and then unlinks it. |fp| is populated
// with the fdopened FILE. |readonly_fd| is populated with the opened fd if
// options.share_read_only is true. |path| is populated with the location of
// the file before it was unlinked.
// Returns false if there's an unhandled failure.
bool CreateAnonymousSharedMemory(const SharedMemoryCreateOptions& options,
                                 ScopedFILE* fp,
                                 ScopedFD* readonly_fd,
                                 FilePath* path);

// Takes the outputs of CreateAnonymousSharedMemory and maps them properly to
// |mapped_file| or |readonly_mapped_file|, depending on which one is populated.
bool PrepareMapFile(ScopedFILE fp,
                    ScopedFD readonly_fd,
                    int* mapped_file,
                    int* readonly_mapped_file);
#endif

}  // namespace base

#endif  // BASE_MEMORY_SHARED_MEMORY_HELPER_H_
