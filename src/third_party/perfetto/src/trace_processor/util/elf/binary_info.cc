/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/util/elf/binary_info.h"

#include <fcntl.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/util/elf/elf.h"

namespace perfetto::trace_processor::elf {
namespace {

bool InRange(const void* base,
             size_t total_size,
             const void* ptr,
             size_t size) {
  return ptr >= base && static_cast<const char*>(ptr) + size <=
                            static_cast<const char*>(base) + total_size;
}
// TODO(rasikanavarange): ETM registers files with large sizes causing size in
// TraceBlobView to become truncated. This means we can not trust any of the
// below checks for large files. So a solution is needed that is not too
// expensive memory wise. b/438916722
template <typename E>
std::optional<uint64_t> GetElfLoadBias(const void* mem, size_t size) {
  const typename E::Ehdr* ehdr = static_cast<const typename E::Ehdr*>(mem);
  if (!InRange(mem, size, ehdr, sizeof(typename E::Ehdr))) {
    PERFETTO_ELOG("Corrupted ELF.");
    return std::nullopt;
  }
  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    const typename E::Phdr* phdr = GetPhdr<E>(mem, ehdr, i);
    if (!InRange(mem, size, phdr, sizeof(typename E::Phdr))) {
      PERFETTO_ELOG("Corrupted ELF.");
      return std::nullopt;
    }
    if (phdr->p_type == PT_LOAD && phdr->p_flags & PF_X) {
      return phdr->p_vaddr - phdr->p_offset;
    }
  }
  return 0u;
}

template <typename E>
std::optional<std::string> GetElfBuildId(const void* mem, size_t size) {
  const typename E::Ehdr* ehdr = static_cast<const typename E::Ehdr*>(mem);
  if (!InRange(mem, size, ehdr, sizeof(typename E::Ehdr))) {
    PERFETTO_ELOG("Corrupted ELF.");
    return std::nullopt;
  }
  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    const typename E::Shdr* shdr = GetShdr<E>(mem, ehdr, i);
    if (!InRange(mem, size, shdr, sizeof(typename E::Shdr))) {
      PERFETTO_ELOG("Corrupted ELF.");
      return std::nullopt;
    }

    if (shdr->sh_type != SHT_NOTE)
      continue;

    auto offset = shdr->sh_offset;
    while (offset < shdr->sh_offset + shdr->sh_size) {
      const typename E::Nhdr* nhdr = reinterpret_cast<const typename E::Nhdr*>(
          static_cast<const char*>(mem) + offset);

      if (!InRange(mem, size, nhdr, sizeof(typename E::Nhdr))) {
        PERFETTO_ELOG("Corrupted ELF.");
        return std::nullopt;
      }
      if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == 4) {
        const char* name = reinterpret_cast<const char*>(nhdr) + sizeof(*nhdr);
        if (!InRange(mem, size, name, 4)) {
          PERFETTO_ELOG("Corrupted ELF.");
          return std::nullopt;
        }
        if (memcmp(name, "GNU", 3) == 0) {
          const char* value = reinterpret_cast<const char*>(nhdr) +
                              sizeof(*nhdr) + base::AlignUp<4>(nhdr->n_namesz);

          if (!InRange(mem, size, value, nhdr->n_descsz)) {
            PERFETTO_ELOG("Corrupted ELF.");
            return std::nullopt;
          }
          return std::string(value, nhdr->n_descsz);
        }
      }
      offset += sizeof(*nhdr) + base::AlignUp<4>(nhdr->n_namesz) +
                base::AlignUp<4>(nhdr->n_descsz);
    }
  }
  return std::nullopt;
}

constexpr uint32_t kMachO64Magic = 0xfeedfacf;

bool IsMachO64(const uint8_t* mem, size_t size) {
  if (size < sizeof(kMachO64Magic))
    return false;
  return memcmp(mem, &kMachO64Magic, sizeof(kMachO64Magic)) == 0;
}

struct mach_header_64 {
  uint32_t magic;      /* mach magic number identifier */
  int32_t cputype;     /* cpu specifier */
  int32_t cpusubtype;  /* machine specifier */
  uint32_t filetype;   /* type of file */
  uint32_t ncmds;      /* number of load commands */
  uint32_t sizeofcmds; /* the size of all the load commands */
  uint32_t flags;      /* flags */
  uint32_t reserved;   /* reserved */
};

struct load_command {
  uint32_t cmd;     /* type of load command */
  uint32_t cmdsize; /* total size of command in bytes */
};

struct segment_64_command {
  uint32_t cmd;      /* LC_SEGMENT_64 */
  uint32_t cmdsize;  /* includes sizeof section_64 structs */
  char segname[16];  /* segment name */
  uint64_t vmaddr;   /* memory address of this segment */
  uint64_t vmsize;   /* memory size of this segment */
  uint64_t fileoff;  /* file offset of this segment */
  uint64_t filesize; /* amount to map from the file */
  uint32_t maxprot;  /* maximum VM protection */
  uint32_t initprot; /* initial VM protection */
  uint32_t nsects;   /* number of sections in segment */
  uint32_t flags;    /* flags */
};

std::optional<BinaryInfo> GetMachOBinaryInfo(const uint8_t* mem, size_t size) {
  if (size < sizeof(mach_header_64))
    return {};

  mach_header_64 header;
  memcpy(&header, mem, sizeof(mach_header_64));

  if (size < sizeof(mach_header_64) + header.sizeofcmds)
    return {};

  std::optional<std::string> build_id;
  uint64_t load_bias = 0;

  const uint8_t* pcmd = mem + sizeof(mach_header_64);
  const uint8_t* pcmds_end = pcmd + header.sizeofcmds;
  while (pcmd < pcmds_end) {
    load_command cmd_header;
    memcpy(&cmd_header, pcmd, sizeof(load_command));

    constexpr uint32_t LC_SEGMENT_64 = 0x19;
    constexpr uint32_t LC_UUID = 0x1b;

    switch (cmd_header.cmd) {
      case LC_UUID: {
        build_id = std::string(
            reinterpret_cast<const char*>(pcmd) + sizeof(load_command),
            cmd_header.cmdsize - sizeof(load_command));
        break;
      }
      case LC_SEGMENT_64: {
        segment_64_command seg_cmd;
        memcpy(&seg_cmd, pcmd, sizeof(segment_64_command));
        if (strcmp(seg_cmd.segname, "__TEXT") == 0) {
          load_bias = seg_cmd.vmaddr;
        }
        break;
      }
      default:
        break;
    }

    pcmd += cmd_header.cmdsize;
  }

  if (build_id) {
    constexpr uint32_t MH_DSYM = 0xa;
    BinaryType type = header.filetype == MH_DSYM ? BinaryType::kMachODsym
                                                 : BinaryType::kMachO;
    return BinaryInfo{*build_id, load_bias, type};
  }
  return {};
}

}  // namespace

bool IsElf(const uint8_t* mem, size_t size) {
  if (size <= EI_MAG3)
    return false;
  return (mem[EI_MAG0] == ELFMAG0 && mem[EI_MAG1] == ELFMAG1 &&
          mem[EI_MAG2] == ELFMAG2 && mem[EI_MAG3] == ELFMAG3);
}

std::optional<BinaryInfo> GetBinaryInfo(const uint8_t* mem, size_t size) {
  static_assert(EI_MAG3 + 1 == sizeof(kMachO64Magic));
  static_assert(EI_CLASS > EI_MAG3, "mem[EI_MAG?] accesses are in range.");
  if (size <= EI_CLASS) {
    return std::nullopt;
  }
  std::optional<std::string> build_id;
  std::optional<uint64_t> load_bias;
  if (IsElf(mem, size)) {
    switch (mem[EI_CLASS]) {
      case ELFCLASS32:
        build_id = GetElfBuildId<Elf32>(mem, size);
        load_bias = GetElfLoadBias<Elf32>(mem, size);
        break;
      case ELFCLASS64:
        build_id = GetElfBuildId<Elf64>(mem, size);
        load_bias = GetElfLoadBias<Elf64>(mem, size);
        break;
      default:
        return std::nullopt;
    }
    if (load_bias) {
      return BinaryInfo{build_id, *load_bias, BinaryType::kElf};
    }
  } else if (IsMachO64(mem, size)) {
    return GetMachOBinaryInfo(mem, size);
  }
  return std::nullopt;
}

}  // namespace perfetto::trace_processor::elf
