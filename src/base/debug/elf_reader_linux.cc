// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/elf_reader_linux.h"

#include <arpa/inet.h>
#include <elf.h>

#include <vector>

#include "base/bits.h"
#include "base/containers/span.h"
#include "base/sha1.h"
#include "base/strings/stringprintf.h"

namespace base {
namespace debug {

namespace {

#if __SIZEOF_POINTER__ == 4
using Ehdr = Elf32_Ehdr;
using Dyn = Elf32_Dyn;
using Half = Elf32_Half;
using Nhdr = Elf32_Nhdr;
using Phdr = Elf32_Phdr;
using Word = Elf32_Word;
#else
using Ehdr = Elf64_Ehdr;
using Dyn = Elf64_Dyn;
using Half = Elf64_Half;
using Nhdr = Elf64_Nhdr;
using Phdr = Elf64_Phdr;
using Word = Elf64_Word;
#endif

using ElfSegment = span<const char>;

Optional<std::string> ElfSegmentBuildIDNoteAsString(const ElfSegment& segment) {
  const void* section_end = segment.data() + segment.size_bytes();
  const Nhdr* note_header = reinterpret_cast<const Nhdr*>(segment.data());
  while (note_header < section_end) {
    if (note_header->n_type == NT_GNU_BUILD_ID)
      break;
    note_header = reinterpret_cast<const Nhdr*>(
        reinterpret_cast<const char*>(note_header) + sizeof(Nhdr) +
        bits::Align(note_header->n_namesz, 4) +
        bits::Align(note_header->n_descsz, 4));
  }

  if (note_header >= section_end || note_header->n_descsz != kSHA1Length)
    return nullopt;

  const uint8_t* guid = reinterpret_cast<const uint8_t*>(note_header) +
                        sizeof(Nhdr) + bits::Align(note_header->n_namesz, 4);

  uint32_t dword = htonl(*reinterpret_cast<const int32_t*>(guid));
  uint16_t word1 = htons(*reinterpret_cast<const int16_t*>(guid + 4));
  uint16_t word2 = htons(*reinterpret_cast<const int16_t*>(guid + 6));
  std::string identifier;
  identifier.reserve(kSHA1Length * 2);  // as hex string
  SStringPrintf(&identifier, "%08X%04X%04X", dword, word1, word2);
  for (size_t i = 8; i < note_header->n_descsz; ++i)
    StringAppendF(&identifier, "%02X", guid[i]);

  return identifier;
}

std::vector<ElfSegment> FindElfSegments(const void* elf_mapped_base,
                                        uint32_t segment_type) {
  const char* elf_base = reinterpret_cast<const char*>(elf_mapped_base);
  if (strncmp(elf_base, ELFMAG, SELFMAG) != 0)
    return std::vector<ElfSegment>();

  const Ehdr* elf_header = reinterpret_cast<const Ehdr*>(elf_base);
  const Phdr* phdrs =
      reinterpret_cast<const Phdr*>(elf_base + elf_header->e_phoff);
  std::vector<ElfSegment> segments;
  for (Half i = 0; i < elf_header->e_phnum; ++i) {
    if (phdrs[i].p_type == segment_type)
      segments.push_back({elf_base + phdrs[i].p_offset, phdrs[i].p_filesz});
  }
  return segments;
}

}  // namespace

Optional<std::string> ReadElfBuildId(const void* elf_base) {
  // Elf program headers can have multiple PT_NOTE arrays.
  std::vector<ElfSegment> segs = FindElfSegments(elf_base, PT_NOTE);
  if (segs.empty())
    return nullopt;
  Optional<std::string> id;
  for (const ElfSegment& seg : segs) {
    id = ElfSegmentBuildIDNoteAsString(seg);
    if (id)
      return id;
  }

  return nullopt;
}

Optional<std::string> ReadElfLibraryName(const void* elf_base) {
  std::vector<ElfSegment> segs = FindElfSegments(elf_base, PT_DYNAMIC);
  if (segs.empty())
    return nullopt;
  DCHECK_EQ(1u, segs.size());

  const ElfSegment& dynamic_seg = segs.front();
  const Dyn* dynamic_start = reinterpret_cast<const Dyn*>(dynamic_seg.data());
  const Dyn* dynamic_end = reinterpret_cast<const Dyn*>(
      dynamic_seg.data() + dynamic_seg.size_bytes());
  Optional<std::string> soname;
  Word soname_strtab_offset = 0;
  const char* strtab_addr = 0;
  for (const Dyn* dynamic_iter = dynamic_start; dynamic_iter < dynamic_end;
       ++dynamic_iter) {
    if (dynamic_iter->d_tag == DT_STRTAB) {
      strtab_addr =
          dynamic_iter->d_un.d_ptr + reinterpret_cast<const char*>(elf_base);
    } else if (dynamic_iter->d_tag == DT_SONAME) {
      soname_strtab_offset = dynamic_iter->d_un.d_val;
    }
  }
  if (soname_strtab_offset && strtab_addr)
    return std::string(strtab_addr + soname_strtab_offset);
  return nullopt;
}

}  // namespace debug
}  // namespace base
