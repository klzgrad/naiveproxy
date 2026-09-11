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

#include "src/trace_processor/util/symbolizer/local_symbolizer.h"

#include <fcntl.h>
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/util/symbolizer/elf.h"
#include "src/trace_processor/util/symbolizer/filesystem.h"
#include "src/trace_processor/util/symbolizer/symbolizer.h"

namespace perfetto::profiling {

#if PERFETTO_BUILDFLAG(PERFETTO_LOCAL_SYMBOLIZER)
namespace {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
constexpr const char* kDefaultSymbolizer = "llvm-symbolizer.exe";
#else
constexpr const char* kDefaultSymbolizer = "llvm-symbolizer";
#endif

std::string GetLine(const std::function<int64_t(char*, size_t)>& fn_read) {
  std::string line;
  char buffer[512];
  int64_t rd = 0;
  while ((rd = fn_read(buffer, sizeof(buffer))) > 0) {
    std::string data(buffer, static_cast<size_t>(rd));
    line += data;
    if (line.back() == '\n') {
      break;
    }
    // There should be no intermediate new lines in the read data.
    PERFETTO_DCHECK(line.find('\n') == std::string::npos);
  }
  if (rd == -1) {
    PERFETTO_ELOG("Failed to read data from subprocess.");
  }
  return line;
}

constexpr size_t kMaxNoteSize = 1u << 20;

// Reads up to |len| bytes at |offset| from |fd| into |buf|. Returns the
// number of bytes actually read (0 on error or EOF); a short read means the
// file is smaller than |len| (e.g. a truncated header).
size_t ReadFileAt(int fd, size_t offset, void* buf, size_t len) {
  if (len == 0)
    return 0;
  if (!base::SeekFile(fd, offset))
    return 0;
  ssize_t rd = base::Read(fd, buf, len);
  return rd > 0 ? static_cast<size_t>(rd) : 0;
}

// Scans the note data at |data| (a PT_NOTE segment or a SHT_NOTE section)
// for the GNU build-id (an NT_GNU_BUILD_ID note named "GNU").
template <typename E>
std::optional<std::string> GetBuildIdFromNotes(const char* data, size_t size) {
  size_t offset = 0;
  while (offset + sizeof(typename E::Nhdr) <= size) {
    typename E::Nhdr nhdr;
    memcpy(&nhdr, data + offset, sizeof(nhdr));
    const size_t name_size = base::AlignUp<4>(nhdr.n_namesz);
    const size_t desc_size = base::AlignUp<4>(nhdr.n_descsz);
    if (offset + sizeof(nhdr) + name_size + desc_size > size)
      return std::nullopt;  // Truncated note.
    if (nhdr.n_type == NT_GNU_BUILD_ID && nhdr.n_namesz == 4 &&
        memcmp(data + offset + sizeof(nhdr), "GNU", 3) == 0) {
      return std::string(data + offset + sizeof(nhdr) + name_size,
                         nhdr.n_descsz);
    }
    offset += sizeof(nhdr) + name_size + desc_size;
  }
  return std::nullopt;
}

// Fallback build-id extraction: walks the section header table (read at
// e_shoff) looking for SHT_NOTE sections. Used only when the fast PT_NOTE
// path found nothing, e.g. files whose build-id note is section-only (some
// ET_REL objects and older linkers).
template <typename E>
std::optional<std::string> GetBuildIdFromShdrs(int fd,
                                               const typename E::Ehdr& ehdr) {
  if (ehdr.e_shnum == 0 || ehdr.e_shoff == 0)
    return std::nullopt;
  const size_t shdr_table_size =
      static_cast<size_t>(ehdr.e_shnum) * sizeof(typename E::Shdr);
  std::string shdrs;
  shdrs.resize(shdr_table_size);
  // Tolerate a truncated section header table (declared e_shnum larger than
  // the actual table): process only the complete entries.
  const size_t num_shdrs = ReadFileAt(fd, static_cast<size_t>(ehdr.e_shoff),
                                      shdrs.data(), shdr_table_size) /
                           sizeof(typename E::Shdr);
  for (size_t i = 0; i < num_shdrs; i++) {
    typename E::Shdr shdr;
    memcpy(&shdr, shdrs.data() + i * sizeof(shdr), sizeof(shdr));
    if (shdr.sh_type != SHT_NOTE)
      continue;
    // Build-id notes are tiny; cap the read to avoid huge allocations from
    // corrupt headers.
    const size_t note_size = static_cast<size_t>(shdr.sh_size);
    if (note_size == 0 || note_size > kMaxNoteSize)
      continue;
    std::string note;
    note.resize(note_size);
    if (ReadFileAt(fd, static_cast<size_t>(shdr.sh_offset), note.data(),
                   note_size) == note_size) {
      if (auto build_id = GetBuildIdFromNotes<E>(note.data(), note.size()))
        return build_id;
    }
  }
  return std::nullopt;
}

std::string SplitBuildID(const std::string& hex_build_id) {
  if (hex_build_id.size() < 3) {
    PERFETTO_DFATAL_OR_ELOG("Invalid build-id (< 3 char) %s",
                            hex_build_id.c_str());
    return {};
  }

  return hex_build_id.substr(0, 2) + "/" + hex_build_id.substr(2);
}

bool IsElf(const char* mem, size_t size) {
  constexpr size_t kElfMagicSize = sizeof(kElfMagic) - 1;
  return size >= kElfMagicSize && memcmp(mem, kElfMagic, kElfMagicSize) == 0;
}

constexpr uint32_t kMachO64Magic = 0xfeedfacf;

bool IsMachO64(const char* mem, size_t size) {
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

struct BinaryInfo {
  std::optional<std::string> build_id;
  std::optional<LoadInfo> load_info;
  BinaryType type;
};

std::optional<BinaryInfo> GetMachOBinaryInfo(int fd,
                                             size_t size,
                                             const mach_header_64& header) {
  if (size < sizeof(header) || header.sizeofcmds > size - sizeof(header))
    return std::nullopt;

  std::string commands;
  commands.resize(header.sizeofcmds);
  if (ReadFileAt(fd, sizeof(header), commands.data(), commands.size()) !=
      commands.size()) {
    return std::nullopt;
  }

  std::optional<std::string> build_id;
  uint64_t vaddr = 0;
  size_t offset = 0;
  while (offset < commands.size()) {
    if (commands.size() - offset < sizeof(load_command))
      return std::nullopt;
    load_command command;
    memcpy(&command, commands.data() + offset, sizeof(command));
    if (command.cmdsize < sizeof(command) ||
        command.cmdsize > commands.size() - offset) {
      return std::nullopt;
    }

    constexpr uint32_t LC_SEGMENT_64 = 0x19;
    constexpr uint32_t LC_UUID = 0x1b;
    switch (command.cmd) {
      case LC_UUID:
        build_id = std::string(commands.data() + offset + sizeof(command),
                               command.cmdsize - sizeof(command));
        break;
      case LC_SEGMENT_64: {
        if (command.cmdsize < sizeof(segment_64_command))
          return std::nullopt;
        segment_64_command segment;
        memcpy(&segment, commands.data() + offset, sizeof(segment));
        constexpr char kTextSegment[] = "__TEXT";
        if (memcmp(segment.segname, kTextSegment, sizeof(kTextSegment)) == 0)
          vaddr = segment.vmaddr;
        break;
      }
      default:
        break;
    }
    offset += command.cmdsize;
  }

  if (!build_id)
    return std::nullopt;
  constexpr uint32_t MH_DSYM = 0xa;
  BinaryType type =
      header.filetype == MH_DSYM ? BinaryType::kMachODsym : BinaryType::kMachO;
  return BinaryInfo{build_id, LoadInfo{vaddr, 0, 0}, type};
}

// Parses the ELF at |fd| (position 0) and extracts build-id and load info by
// reading only the ELF header, the program header table and the build-id note
// (PT_NOTE). The section header table at the end of large binaries is only
// read as a fallback when the note is not in a program segment.
template <typename E>
std::optional<BinaryInfo> GetElfBinaryInfo(int fd,
                                           const char* header,
                                           size_t header_size) {
  if (header_size < sizeof(typename E::Ehdr))
    return std::nullopt;
  typename E::Ehdr ehdr;
  memcpy(&ehdr, header, sizeof(ehdr));

  std::optional<std::string> build_id;
  std::optional<LoadInfo> load_info;

  // Program header table (e_phnum is 16-bit, so the table is at most a few
  // hundred KB). A short read (truncated table) is tolerated: only the
  // complete entries are processed, and the build-id section fallback still
  // runs.
  const size_t phdr_table_size =
      static_cast<size_t>(ehdr.e_phnum) * sizeof(typename E::Phdr);
  if (phdr_table_size > 0 && ehdr.e_phoff != 0) {
    std::string phdrs;
    phdrs.resize(phdr_table_size);
    const size_t num_phdrs = ReadFileAt(fd, static_cast<size_t>(ehdr.e_phoff),
                                        phdrs.data(), phdr_table_size) /
                             sizeof(typename E::Phdr);
    for (size_t i = 0; i < num_phdrs; i++) {
      typename E::Phdr phdr;
      memcpy(&phdr, phdrs.data() + i * sizeof(phdr), sizeof(phdr));
      if (phdr.p_type == PT_LOAD && phdr.p_flags & PF_X) {
        // p_align can only be 0, 1 (no alignment requirement) or a power of
        // two.
        if (phdr.p_align != 0 && !base::IsPowerOfTwo(phdr.p_align)) {
          PERFETTO_DLOG("Invalid p_align value: %" PRIu64,
                        static_cast<uint64_t>(phdr.p_align));
          return std::nullopt;
        }
        if (!load_info.has_value()) {
          load_info = LoadInfo{phdr.p_vaddr, phdr.p_offset, phdr.p_align};
        }
      } else if (phdr.p_type == PT_NOTE && !build_id.has_value()) {
        // Build-id notes are tiny; cap the read to avoid huge allocations
        // from corrupt headers.
        const size_t note_size = static_cast<size_t>(phdr.p_filesz);
        if (note_size > 0 && note_size <= kMaxNoteSize) {
          std::string note;
          note.resize(note_size);
          if (ReadFileAt(fd, static_cast<size_t>(phdr.p_offset), note.data(),
                         note_size) == note_size) {
            build_id = GetBuildIdFromNotes<E>(note.data(), note.size());
          }
        }
      }
    }
  }

  if (!build_id.has_value())
    build_id = GetBuildIdFromShdrs<E>(fd, ehdr);

  return BinaryInfo{build_id, load_info, BinaryType::kElf};
}

// Parses an already-open file whose format has not yet been detected. Reads
// the initial header once and dispatches to the ELF (32/64) or Mach-O parser.
// |is_binary| distinguishes non-binary files from malformed binaries.
std::optional<BinaryInfo> GetBinaryInfoFromFd(int fd,
                                              size_t size,
                                              bool* is_binary = nullptr) {
  static_assert(sizeof(mach_header_64) <= sizeof(Elf64::Ehdr));
  char header[sizeof(Elf64::Ehdr)];
  const size_t header_size = ReadFileAt(fd, 0, header, sizeof(header));
  const bool is_elf = IsElf(header, header_size);
  const bool is_macho = IsMachO64(header, header_size);
  if (is_binary)
    *is_binary = is_elf || is_macho;

  if (is_elf) {
    if (header_size <= EI_CLASS)
      return std::nullopt;
    switch (header[EI_CLASS]) {
      case ELFCLASS32:
        return GetElfBinaryInfo<Elf32>(fd, header, header_size);
      case ELFCLASS64:
        return GetElfBinaryInfo<Elf64>(fd, header, header_size);
      default:
        return std::nullopt;
    }
  }
  if (is_macho) {
    if (header_size < sizeof(mach_header_64))
      return std::nullopt;
    mach_header_64 mach_header;
    memcpy(&mach_header, header, sizeof(mach_header));
    return GetMachOBinaryInfo(fd, size, mach_header);
  }
  return std::nullopt;
}

std::optional<BinaryInfo> GetBinaryInfo(const char* fname, size_t size) {
  base::ScopedFile fd(base::OpenFile(fname, O_RDONLY));
  if (!fd) {
    return std::nullopt;
  }
  return GetBinaryInfoFromFd(fd.get(), size);
}

// Helper function to process a single binary file and add it to the index.
// Increments *corrupt_file_count when a file looks like ELF/Mach-O but cannot
// be parsed (corrupt or truncated): callers aggregate this into a single log
// line instead of spamming one message per file.
void ProcessBinaryFile(const char* fname,
                       size_t size,
                       std::map<std::string, FoundBinary>& result,
                       uint32_t* corrupt_file_count) {
  base::ScopedFile fd(base::OpenFile(fname, O_RDONLY));
  if (!fd) {
    // Missing/unreadable file: skip silently (e.g. speculative paths).
    return;
  }
  bool is_binary = false;
  std::optional<BinaryInfo> binary_info =
      GetBinaryInfoFromFd(fd.get(), size, &is_binary);
  if (!is_binary) {
    PERFETTO_DLOG("%s not an ELF or Mach-O 64.", fname);
    return;
  }
  if (!binary_info) {
    // Passed the magic check but failed to parse: corrupt or truncated.
    ++(*corrupt_file_count);
    return;
  }
  if (!binary_info->build_id) {
    PERFETTO_DLOG("Failed to extract build id from %s.", fname);
    return;
  }
  if (!binary_info->load_info) {
    PERFETTO_DLOG("Failed to extract load info from %s.", fname);
    return;
  }
  auto [it, inserted] =
      result.emplace(*binary_info->build_id, FoundBinary{
                                                 fname,
                                                 *binary_info->load_info,
                                                 binary_info->type,
                                             });

  if (inserted) {
    PERFETTO_DLOG("Indexed: %s (%s)", fname,
                  base::ToHex(*binary_info->build_id).c_str());
    return;
  }

  // If there was already an existing FoundBinary, the emplace wouldn't insert
  // anything. But, for Mac binaries, we prefer dSYM files over the original
  // binary, so make sure these overwrite the FoundBinary entry.
  if (it->second.type == BinaryType::kMachO &&
      binary_info->type == BinaryType::kMachODsym) {
    PERFETTO_LOG("Overwriting index entry for %s to %s.",
                 base::ToHex(*binary_info->build_id).c_str(), fname);
    it->second = FoundBinary{fname, *binary_info->load_info, binary_info->type};
  } else {
    PERFETTO_DLOG("Ignoring %s, index entry for %s already exists.", fname,
                  base::ToHex(*binary_info->build_id).c_str());
  }
}

std::map<std::string, FoundBinary> BuildIdIndex(
    std::vector<std::string> dirs,
    std::vector<std::string> files) {
  std::map<std::string, FoundBinary> result;
  uint32_t corrupt_file_count = 0;

  // Process directories
  if (!dirs.empty()) {
    WalkDirectories(std::move(dirs), [&result, &corrupt_file_count](
                                         const char* fname, size_t size) {
      ProcessBinaryFile(fname, size, result, &corrupt_file_count);
    });
  }

  // Process individual files
  for (const std::string& file_path : files) {
    std::optional<uint64_t> file_size = base::GetFileSize(file_path);
    if (!file_size.has_value()) {
      continue;
    }
    // Unlike WalkDirectories we don't have the size on hand here; pass the
    // real size so GetBinaryInfo can actually parse the file. A size of 0
    // would make it bail out early and miscount every valid file as corrupt.
    size_t size = static_cast<size_t>(
        std::min<uint64_t>(std::numeric_limits<size_t>::max(), *file_size));
    ProcessBinaryFile(file_path.c_str(), size, result, &corrupt_file_count);
  }

  if (corrupt_file_count > 0) {
    PERFETTO_LOG(
        "Skipped %u file(s) that look like ELF/Mach-O but could not be "
        "parsed (corrupt or truncated) while indexing symbol paths.",
        corrupt_file_count);
  }

  return result;
}

bool ParseJsonString(const char*& it, const char* end, std::string* out) {
  *out = "";
  if (it == end) {
    return false;
  }
  if (*it++ != '"') {
    return false;
  }
  while (true) {
    if (it == end) {
      return false;
    }
    char c = *it++;
    if (c == '"') {
      return true;
    }
    if (c == '\\') {
      if (it == end) {
        return false;
      }
      c = *it++;
      switch (c) {
        case '"':
        case '\\':
        case '/':
          out->push_back(c);
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 't':
          out->push_back('\t');
          break;
        // Pass-through \u escape codes without re-encoding to utf-8, for
        // simplicity.
        case 'u':
          out->push_back('\\');
          out->push_back('u');
          break;
        default:
          return false;
      }
    } else {
      out->push_back(c);
    }
  }
}

bool ParseJsonNumber(const char*& it, const char* end, double* out) {
  bool is_minus = false;
  double ret = 0;
  if (it == end) {
    return false;
  }
  if (*it == '-') {
    ++it;
    is_minus = true;
  }
  while (true) {
    if (it == end) {
      return false;
    }
    char c = *it++;
    if (isdigit(c)) {
      ret = ret * 10 + (c - '0');
    } else if (c == 'e') {
      // Scientific syntax is not supported.
      return false;
    } else {
      // Unwind the iterator to point at the end of the number.
      it--;
      break;
    }
  }
  *out = is_minus ? -ret : ret;
  return true;
}

bool ParseJsonArray(
    const char*& it,
    const char* end,
    const std::function<bool(const char*&, const char*)>& process_value) {
  if (it == end) {
    return false;
  }
  char c = *it++;
  if (c != '[') {
    return false;
  }
  while (true) {
    if (!process_value(it, end)) {
      return false;
    }
    if (it == end) {
      return false;
    }
    c = *it++;
    if (c == ']') {
      return true;
    }
    if (c != ',') {
      return false;
    }
  }
}

bool ParseJsonObject(
    const char*& it,
    const char* end,
    const std::function<bool(const char*&, const char*, const std::string&)>&
        process_value) {
  if (it == end) {
    return false;
  }
  char c = *it++;
  if (c != '{') {
    return false;
  }
  while (true) {
    std::string key;
    if (!ParseJsonString(it, end, &key)) {
      return false;
    }
    if (*it++ != ':') {
      return false;
    }
    if (!process_value(it, end, key)) {
      return false;
    }
    if (it == end) {
      return false;
    }
    c = *it++;
    if (c == '}') {
      return true;
    }
    if (c != ',') {
      return false;
    }
  }
}

bool SkipJsonValue(const char*& it, const char* end) {
  if (it == end) {
    return false;
  }
  char c = *it;
  if (c == '"') {
    std::string ignored;
    return ParseJsonString(it, end, &ignored);
  }
  if (isdigit(c) || c == '-') {
    double ignored;
    return ParseJsonNumber(it, end, &ignored);
  }
  if (c == '[') {
    return ParseJsonArray(it, end, [](const char*& it, const char* end) {
      return SkipJsonValue(it, end);
    });
  }
  if (c == '{') {
    return ParseJsonObject(
        it, end, [](const char*& it, const char* end, const std::string&) {
          return SkipJsonValue(it, end);
        });
  }
  return false;
}

std::optional<FoundBinary> IsCorrectFile(
    const std::string& symbol_file,
    std::optional<std::string_view> build_id,
    BinaryPathError* error) {
  if (!base::FileExists(symbol_file)) {
    *error = BinaryPathError::kFileNotFound;
    return std::nullopt;
  }
  *error = BinaryPathError::kParseError;
  // Openfile opens the file with an exclusive lock on windows.
  std::optional<uint64_t> file_size = base::GetFileSize(symbol_file);
  if (!file_size.has_value()) {
    return std::nullopt;
  }

  static_assert(sizeof(size_t) <= sizeof(uint64_t));
  size_t size = static_cast<size_t>(
      std::min<uint64_t>(std::numeric_limits<size_t>::max(), *file_size));

  if (size == 0) {
    return std::nullopt;
  }

  std::optional<BinaryInfo> binary_info =
      GetBinaryInfo(symbol_file.c_str(), size);
  if (!binary_info)
    return std::nullopt;
  if (!binary_info->load_info)
    return std::nullopt;
  if (build_id && binary_info->build_id != *build_id) {
    *error = BinaryPathError::kBuildIdMismatch;
    return std::nullopt;
  }
  *error = BinaryPathError::kOk;
  return FoundBinary{symbol_file, *binary_info->load_info, binary_info->type};
}

// Try a path and record the attempt.
// Returns true if the binary was found.
bool TryPath(const std::string& path,
             const std::string& build_id,
             std::optional<FoundBinary>& out_binary,
             std::vector<BinaryPathAttempt>& attempts) {
  if (!base::FileExists(path)) {
    attempts.push_back({path, BinaryPathError::kFileNotFound});
    return false;
  }
  BinaryPathError error;
  std::optional<FoundBinary> found = IsCorrectFile(path, build_id, &error);
  if (found) {
    out_binary = std::move(found);
    attempts.push_back({path, BinaryPathError::kOk});
    return true;
  }
  attempts.push_back({path, error});
  return false;
}

std::optional<FoundBinary> FindBinaryInRoot(
    const std::string& root_str,
    const std::string& abspath,
    const std::string& build_id,
    std::vector<BinaryPathAttempt>& attempts) {
  constexpr char kApkPrefix[] = "base.apk!";

  std::optional<FoundBinary> result;
  // Try a path relative to the symbol root and record the attempt.
  auto try_path_in_root = [&](const std::string& rel_path) {
    return TryPath(root_str + "/" + rel_path, build_id, result, attempts);
  };

  // Strip the leading root (e.g. "/" or "C:\") before searching for the
  // mapping under `root_str`. Treat both '/' and '\' as path separators.
  std::string_view rel = abspath;
  rel.remove_prefix(base::PathRootPrefixLength(abspath));
  size_t last_sep = rel.find_last_of("/\\");
  size_t file_pos = last_sep == std::string_view::npos ? 0 : last_sep + 1;

  std::string filename(rel.substr(file_pos));

  // Return the first match for the following options:
  // * absolute path of library file relative to root.
  // * absolute path of library file relative to root, but with base.apk!
  //   removed from filename.
  // * only filename of library file relative to root.
  // * only filename of library file relative to root, but with base.apk!
  //   removed from filename.
  // * in the subdirectory .build-id: the first two hex digits of the build-id
  //   as subdirectory, then the rest of the hex digits, with ".debug"appended.
  //   See
  //   https://fedoraproject.org/wiki/RolandMcGrath/BuildID#Find_files_by_build_ID
  //
  // For example, "/system/lib/base.apk!foo.so" with build id abcd1234,
  // is looked for at
  // * $ROOT/system/lib/base.apk!foo.so
  // * $ROOT/system/lib/foo.so
  // * $ROOT/base.apk!foo.so
  // * $ROOT/foo.so
  // * $ROOT/.build-id/ab/cd1234.debug

  // Directory of the mapping relative to the root, including the trailing '/'
  // (empty if the mapping has no directory component). Native separators are
  // normalized to '/', which is accepted on all platforms.
  std::string dir(rel.substr(0, file_pos));
  std::replace(dir.begin(), dir.end(), '\\', '/');

  bool has_apk_prefix = base::StartsWith(filename, kApkPrefix);
  std::string filename_without_apk_prefix =
      has_apk_prefix ? filename.substr(sizeof(kApkPrefix) - 1) : std::string();

  if (!dir.empty() && try_path_in_root(dir + filename)) {
    return result;
  }
  if (!dir.empty() && has_apk_prefix &&
      try_path_in_root(dir + filename_without_apk_prefix)) {
    return result;
  }
  if (try_path_in_root(filename)) {
    return result;
  }
  if (has_apk_prefix && try_path_in_root(filename_without_apk_prefix)) {
    return result;
  }

  std::string hex_build_id = base::ToHex(build_id.c_str(), build_id.size());
  if (std::string build_id_path = SplitBuildID(hex_build_id);
      !build_id_path.empty() &&
      try_path_in_root(".build-id/" + build_id_path + ".debug")) {
    return result;
  }

  return std::nullopt;
}

std::optional<FoundBinary> FindKernelBinary(
    const std::string& os_release,
    std::vector<BinaryPathAttempt>& attempts) {
  using SS = base::StackString<512>;
  const char* rel = os_release.c_str();

  // Helper to try a kernel path and record the attempt.
  auto try_kernel_path =
      [&](base::StackString<512> path_ss) -> std::optional<FoundBinary> {
    std::string path = path_ss.ToStdString();
    if (!base::FileExists(path)) {
      attempts.push_back({path, BinaryPathError::kFileNotFound});
      return std::nullopt;
    }
    BinaryPathError error;
    std::optional<FoundBinary> found =
        IsCorrectFile(path, std::nullopt, &error);
    if (found) {
      attempts.push_back({path, BinaryPathError::kOk});
      return found;
    }
    attempts.push_back({path, error});
    return std::nullopt;
  };

  // This list comes from the perf symbolization code [1]: it's an incomplete
  // list (it doesn't include pre-symbolized kernels or reading /proc/kallsyms)
  // but works if you just install e.g. the symbol packages for the kernel.
  //
  // [1]
  // https://elixir.bootlin.com/linux/v6.12.2/source/tools/perf/util/symbol.c#L2294
  if (auto b = try_kernel_path(SS("/boot/vmlinux-%s", rel))) {
    return b;
  }
  if (auto b = try_kernel_path(SS("/usr/lib/debug/boot/vmlinux-%s", rel))) {
    return b;
  }
  if (auto b = try_kernel_path(SS("/lib/modules/%s/build/vmlinux", rel))) {
    return b;
  }
  if (auto b =
          try_kernel_path(SS("/usr/lib/debug/lib/modules/%s/vmlinux", rel))) {
    return b;
  }
  if (auto b =
          try_kernel_path(SS("/usr/lib/debug/boot/vmlinux-%s.debug", rel))) {
    return b;
  }
  return std::nullopt;
}

}  // namespace

bool ParseLlvmSymbolizerJsonLine(const std::string& line,
                                 std::vector<SymbolizedFrame>* result) {
  // Parse Json of the format:
  // ```
  // {"Address":"0x1b72f","ModuleName":"...","Symbol":[{"Column":0,
  // "Discriminator":0,"FileName":"...","FunctionName":"...","Line":0,
  // "StartAddress":"","StartFileName":"...","StartLine":0},...]}
  // ```
  const char* it = line.data();
  const char* end = it + line.size();
  return ParseJsonObject(
      it, end, [&](const char*& it, const char* end, const std::string& key) {
        if (key == "Symbol") {
          return ParseJsonArray(it, end, [&](const char*& it, const char* end) {
            SymbolizedFrame frame;
            if (!ParseJsonObject(
                    it, end,
                    [&](const char*& it, const char* end,
                        const std::string& key) {
                      if (key == "FileName") {
                        return ParseJsonString(it, end, &frame.file_name);
                      }
                      if (key == "FunctionName") {
                        return ParseJsonString(it, end, &frame.function_name);
                      }
                      if (key == "Line") {
                        double number;
                        if (!ParseJsonNumber(it, end, &number)) {
                          return false;
                        }
                        frame.line = static_cast<unsigned int>(number);
                        return true;
                      }
                      return SkipJsonValue(it, end);
                    })) {
              return false;
            }
            // Use "??" for empty filenames, to match non-JSON output.
            if (frame.file_name.empty()) {
              frame.file_name = "??";
            }
            result->push_back(frame);
            return true;
          });
        }
        if (key == "Error") {
          std::string message;
          if (!ParseJsonObject(it, end,
                               [&](const char*& it, const char* end,
                                   const std::string& key) {
                                 if (key == "Message") {
                                   return ParseJsonString(it, end, &message);
                                 }
                                 return SkipJsonValue(it, end);
                               })) {
            return false;
          }
          PERFETTO_ELOG("Failed to symbolize: %s.", message.c_str());
          return true;
        }
        return SkipJsonValue(it, end);
      });
}

BinaryFinder::~BinaryFinder() = default;

LocalBinaryIndexer::LocalBinaryIndexer(
    std::vector<std::string> directories,
    std::vector<std::string> individual_files)
    : indexed_directories_(directories),
      symbol_files_(individual_files.begin(), individual_files.end()),
      buildid_to_file_(
          BuildIdIndex(std::move(directories), std::move(individual_files))) {}

BinaryLookupResult LocalBinaryIndexer::FindBinary(const std::string& abspath,
                                                  const std::string& build_id) {
  auto it = buildid_to_file_.find(build_id);
  if (it != buildid_to_file_.end()) {
    // Success - record the successful path lookup.
    return {it->second, {{it->second.file_name, BinaryPathError::kOk}}};
  }
  // Build ID not in index - report what was searched.
  std::vector<BinaryPathAttempt> attempts;
  // If the mapping path was explicitly in symbol_files, report it.
  if (symbol_files_.count(abspath)) {
    attempts.push_back({abspath, BinaryPathError::kFileNotFound});
  }
  // Report all indexed directories.
  for (const std::string& dir : indexed_directories_) {
    attempts.push_back({dir, BinaryPathError::kBuildIdNotInIndex});
  }
  return {{}, std::move(attempts)};
}

LocalBinaryIndexer::~LocalBinaryIndexer() = default;

LocalBinaryFinder::LocalBinaryFinder(std::vector<std::string> roots)
    : roots_(std::move(roots)) {}

BinaryLookupResult LocalBinaryFinder::FindBinary(const std::string& abspath,
                                                 const std::string& build_id) {
  auto p = cache_.emplace(abspath, BinaryLookupResult{});
  if (!p.second)
    return p.first->second;

  BinaryLookupResult& result = p.first->second;

  // Try the absolute path first.
  if (base::IsAbsolutePath(abspath)) {
    if (TryPath(abspath, build_id, result.binary, result.attempts)) {
      return result;
    }
  }

  // Try each root directory.
  for (const std::string& root_str : roots_) {
    std::optional<FoundBinary> found =
        FindBinaryInRoot(root_str, abspath, build_id, result.attempts);
    if (found) {
      result.binary = std::move(found);
      return result;
    }
  }
  return result;
}

LocalBinaryFinder::~LocalBinaryFinder() = default;

LLVMSymbolizerProcess::LLVMSymbolizerProcess(const std::string& symbolizer_path)
    :
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
      subprocess_(symbolizer_path, {"--output-style=JSON"}) {
}
#else
      subprocess_(symbolizer_path, {"llvm-symbolizer", "--output-style=JSON"}) {
}
#endif

std::vector<SymbolizedFrame> LLVMSymbolizerProcess::Symbolize(
    const std::string& binary,
    uint64_t address) {
  std::vector<SymbolizedFrame> result;
  base::StackString<1024> buffer("\"%s\" 0x%" PRIx64 "\n", binary.c_str(),
                                 address);
  if (subprocess_.Write(buffer.c_str(), buffer.len()) < 0) {
    PERFETTO_ELOG("Failed to write to llvm-symbolizer.");
    return result;
  }
  auto line = GetLine([&](char* read_buffer, size_t buffer_size) {
    return subprocess_.Read(read_buffer, buffer_size);
  });
  // llvm-symbolizer writes out records as one JSON per line.
  if (!ParseLlvmSymbolizerJsonLine(line, &result)) {
    PERFETTO_ELOG("Failed to parse llvm-symbolizer JSON: %s", line.c_str());
    return {};
  }
  return result;
}

namespace {
SymbolPathError ToSymbolPathError(BinaryPathError error) {
  switch (error) {
    case BinaryPathError::kOk:
      return SymbolPathError::kOk;
    case BinaryPathError::kFileNotFound:
      return SymbolPathError::kFileNotFound;
    case BinaryPathError::kBuildIdMismatch:
      return SymbolPathError::kBuildIdMismatch;
    case BinaryPathError::kParseError:
      return SymbolPathError::kParseError;
    case BinaryPathError::kBuildIdNotInIndex:
      return SymbolPathError::kBuildIdNotInIndex;
  }
  PERFETTO_FATAL("Unknown BinaryPathError");
}

std::vector<SymbolPathAttempt> ToSymbolPathAttempts(
    const std::vector<BinaryPathAttempt>& attempts) {
  std::vector<SymbolPathAttempt> result;
  result.reserve(attempts.size());
  for (const auto& attempt : attempts) {
    result.push_back({attempt.path, ToSymbolPathError(attempt.error)});
  }
  return result;
}

// `llvm-symbolizer` expects us to provide vaddr values (also called in this
// code base relative pc). These are addresses relative to the preferred load
// address passed to the linker in the ELF program header. The `rel_pc` values
// in the `__intrinsic_stack_profile_frame` table have been converted from
// absolute addresses (the acutal address in the program counter address of the
// CPU) using the `start`, `exact_offset`, `start_offset` and `load_bias` values
// in `__intrinsic_stack_profile_mapping`. But there are multiple situations
// were this conversion is wrong and we need to adjust it:
//   - On Android 10, there was a bug in libunwindstack that would incorrectly
//     calculate the load_bias, and thus the relative PC. This would end up in
//     frames that made no sense. We can fix this up after the fact if we
//     detect this situation (comparing the stored load_bias vs the computed one
//     from the binary).
//   - When reading perf (or simpleperf) files we do not get `load_bias`
//     information so we set the value to zero in
//     `__intrinsic_stack_profile_mapping`. This gives us an incorrect value for
//     `rel_pc`.
//
uint64_t ComputeUserSpaceAddressCorrection(
    const UnsymbolizedMapping& runtime_mapping,
    const FoundBinary& binary) {
  if (binary.type != BinaryType::kElf) {
    return 0;
  }

  const LoadInfo& load_info = binary.load_info;

  // We need the relative offset to the start of the ELF. For perf and
  // simpleperf `start_offset` is 0, but libunwindstack in traced_perf might set
  // it to non zero e.g. for shared libraries in APKs
  uint64_t offset = runtime_mapping.exact_offset - runtime_mapping.start_offset;

  // We need to redo the runtime loaders work here to figure out the load bias.
  // Note we can not trust the p_offset value in `load_info` as
  // debug-symbol-only binaries have "invalid" (as in not the same as binaries
  // with the executable code) values. So we use the runtime offset instead.
  // p_vaddr and p_offset must have congruent values, modulo `p_align`.
  // Attention: p_align can be 0 (means no aligment required)
  uint64_t align_to = load_info.p_align == 0 ? 1 : load_info.p_align;
  uint64_t adj_vaddr = base::AlignDown(load_info.p_vaddr, align_to);
  uint64_t adj_offset = base::AlignDown(offset, align_to);
  uint64_t real_load_bias = adj_vaddr - adj_offset;

  if (real_load_bias > runtime_mapping.load_bias) {
    return real_load_bias - runtime_mapping.load_bias;
  }

  return 0;
}

}  // namespace

SymbolizeResult LocalSymbolizer::Symbolize(
    const Environment& env,
    const UnsymbolizedMapping& mapping,
    const std::vector<uint64_t>& addresses) {
  bool is_kernel = base::StartsWith(mapping.name, "[kernel.kallsyms]");
  std::optional<FoundBinary> binary;
  std::vector<BinaryPathAttempt> binary_attempts;
  if (is_kernel) {
    if (env.os_release) {
      binary = FindKernelBinary(*env.os_release, binary_attempts);
    }
  } else {
    BinaryLookupResult lookup =
        finder_->FindBinary(mapping.name, mapping.build_id);
    binary = std::move(lookup.binary);
    binary_attempts = std::move(lookup.attempts);
  }
  std::vector<SymbolPathAttempt> attempts =
      ToSymbolPathAttempts(binary_attempts);
  if (!binary) {
    return {{}, std::move(attempts)};
  }

  const LoadInfo& load_info = binary->load_info;
  uint64_t addr_correction =
      // When symbolizing kernel frames from Linux perf (*not* simpleperf) we
      // need to add the vaddr because llvm-symbolizer expects that we provide
      // absolute addresses unlike all other files where it expects relative
      // addresses.
      is_kernel ? load_info.p_vaddr
                : ComputeUserSpaceAddressCorrection(mapping, *binary);
  if (addr_correction != 0) {
    PERFETTO_DLOG("Correcting load bias by %" PRIu64 " for %s", addr_correction,
                  mapping.name.c_str());
  }

  SymbolizeResult result;
  result.frames.reserve(addresses.size());
  for (uint64_t address : addresses) {
    result.frames.emplace_back(llvm_symbolizer_.Symbolize(
        binary->file_name, address + addr_correction));
  }
  return result;
}

LocalSymbolizer::LocalSymbolizer(const std::string& symbolizer_path,
                                 std::unique_ptr<BinaryFinder> finder)
    : llvm_symbolizer_(symbolizer_path), finder_(std::move(finder)) {}

LocalSymbolizer::LocalSymbolizer(std::unique_ptr<BinaryFinder> finder)
    : LocalSymbolizer(kDefaultSymbolizer, std::move(finder)) {}

LocalSymbolizer::~LocalSymbolizer() = default;

#endif  // PERFETTO_BUILDFLAG(PERFETTO_LOCAL_SYMBOLIZER)

std::unique_ptr<Symbolizer> MaybeLocalSymbolizer(
    const std::vector<std::string>& directories,
    const std::vector<std::string>& individual_files,
    const char* mode) {
  std::unique_ptr<Symbolizer> symbolizer;

  if (!directories.empty() || !individual_files.empty()) {
#if PERFETTO_BUILDFLAG(PERFETTO_LOCAL_SYMBOLIZER)
    std::unique_ptr<BinaryFinder> finder;
    if (!mode || strncmp(mode, "find", 4) == 0) {
      finder = std::make_unique<LocalBinaryFinder>(std::move(directories));
    } else if (strncmp(mode, "index", 5) == 0) {
      finder = std::make_unique<LocalBinaryIndexer>(
          std::move(directories), std::move(individual_files));
    } else {
      PERFETTO_FATAL("Invalid symbolizer mode [find | index]: %s", mode);
    }
    symbolizer = std::make_unique<LocalSymbolizer>(std::move(finder));
#else
    base::ignore_result(mode);
    PERFETTO_FATAL("This build does not support local symbolization.");
#endif
  }
  return symbolizer;
}

}  // namespace perfetto::profiling
