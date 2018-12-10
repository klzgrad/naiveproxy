// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEBUG_ELF_READER_LINUX_H_
#define BASE_DEBUG_ELF_READER_LINUX_H_

#include <string>

#include "base/base_export.h"
#include "base/optional.h"

namespace base {
namespace debug {

// Returns the ELF section .note.gnu.build-id from the ELF file mapped at
// |elf_base|, if present. The caller must ensure that the file is fully mapped
// in memory.
Optional<std::string> BASE_EXPORT ReadElfBuildId(const void* elf_base);

// Returns the library name from the ELF file mapped at |elf_base|, if present.
// The caller must ensure that the file is fully mapped in memory.
Optional<std::string> BASE_EXPORT ReadElfLibraryName(const void* elf_base);

}  // namespace debug
}  // namespace base

#endif  // BASE_DEBUG_ELF_READER_LINUX_H_
