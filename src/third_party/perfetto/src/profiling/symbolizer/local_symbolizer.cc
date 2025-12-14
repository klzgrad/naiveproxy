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

#include "src/profiling/symbolizer/local_symbolizer.h"

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
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "src/profiling/symbolizer/elf.h"
#include "src/profiling/symbolizer/filesystem.h"
#include "src/profiling/symbolizer/symbolizer.h"

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

bool InRange(const void* base,
             size_t total_size,
             const void* ptr,
             size_t size) {
  return ptr >= base && static_cast<const char*>(ptr) + size <=
                            static_cast<const char*>(base) + total_size;
}

template <typename E>
std::optional<std::pair<uint64_t, uint64_t>> GetElfPVAddrPOffset(void* mem,
                                                                 size_t size) {
  const typename E::Ehdr* ehdr = static_cast<typename E::Ehdr*>(mem);
  if (!InRange(mem, size, ehdr, sizeof(typename E::Ehdr))) {
    PERFETTO_ELOG("Corrupted ELF.");
    return std::nullopt;
  }
  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    typename E::Phdr* phdr = GetPhdr<E>(mem, ehdr, i);
    if (!InRange(mem, size, phdr, sizeof(typename E::Phdr))) {
      PERFETTO_ELOG("Corrupted ELF.");
      return std::nullopt;
    }
    if (phdr->p_type == PT_LOAD && phdr->p_flags & PF_X) {
      return std::make_pair(phdr->p_vaddr, phdr->p_offset);
    }
  }
  return std::make_pair(0, 0);
}

template <typename E>
std::optional<std::string> GetElfBuildId(void* mem, size_t size) {
  const typename E::Ehdr* ehdr = static_cast<typename E::Ehdr*>(mem);
  if (!InRange(mem, size, ehdr, sizeof(typename E::Ehdr))) {
    PERFETTO_ELOG("Corrupted ELF.");
    return std::nullopt;
  }
  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    typename E::Shdr* shdr = GetShdr<E>(mem, ehdr, i);
    if (!InRange(mem, size, shdr, sizeof(typename E::Shdr))) {
      PERFETTO_ELOG("Corrupted ELF.");
      return std::nullopt;
    }

    if (shdr->sh_type != SHT_NOTE)
      continue;

    auto offset = shdr->sh_offset;
    while (offset < shdr->sh_offset + shdr->sh_size) {
      auto* nhdr =
          reinterpret_cast<typename E::Nhdr*>(static_cast<char*>(mem) + offset);

      if (!InRange(mem, size, nhdr, sizeof(typename E::Nhdr))) {
        PERFETTO_ELOG("Corrupted ELF.");
        return std::nullopt;
      }
      if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == 4) {
        char* name = reinterpret_cast<char*>(nhdr) + sizeof(*nhdr);
        if (!InRange(mem, size, name, 4)) {
          PERFETTO_ELOG("Corrupted ELF.");
          return std::nullopt;
        }
        if (memcmp(name, "GNU", 3) == 0) {
          const char* value = reinterpret_cast<char*>(nhdr) + sizeof(*nhdr) +
                              base::AlignUp<4>(nhdr->n_namesz);

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

std::string SplitBuildID(const std::string& hex_build_id) {
  if (hex_build_id.size() < 3) {
    PERFETTO_DFATAL_OR_ELOG("Invalid build-id (< 3 char) %s",
                            hex_build_id.c_str());
    return {};
  }

  return hex_build_id.substr(0, 2) + "/" + hex_build_id.substr(2);
}

bool IsElf(const char* mem, size_t size) {
  if (size <= EI_MAG3)
    return false;
  return (mem[EI_MAG0] == ELFMAG0 && mem[EI_MAG1] == ELFMAG1 &&
          mem[EI_MAG2] == ELFMAG2 && mem[EI_MAG3] == ELFMAG3);
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
  std::string build_id;
  uint64_t p_vaddr;
  uint64_t p_offset;
  BinaryType type;
};

std::optional<BinaryInfo> GetMachOBinaryInfo(char* mem, size_t size) {
  if (size < sizeof(mach_header_64))
    return {};

  mach_header_64 header;
  memcpy(&header, mem, sizeof(mach_header_64));

  if (size < sizeof(mach_header_64) + header.sizeofcmds)
    return {};

  std::optional<std::string> build_id;
  uint64_t vaddr = 0;

  char* pcmd = mem + sizeof(mach_header_64);
  char* pcmds_end = pcmd + header.sizeofcmds;
  while (pcmd < pcmds_end) {
    load_command cmd_header;
    memcpy(&cmd_header, pcmd, sizeof(load_command));

    constexpr uint32_t LC_SEGMENT_64 = 0x19;
    constexpr uint32_t LC_UUID = 0x1b;

    switch (cmd_header.cmd) {
      case LC_UUID: {
        build_id = std::string(pcmd + sizeof(load_command),
                               cmd_header.cmdsize - sizeof(load_command));
        break;
      }
      case LC_SEGMENT_64: {
        segment_64_command seg_cmd;
        memcpy(&seg_cmd, pcmd, sizeof(segment_64_command));
        if (strcmp(seg_cmd.segname, "__TEXT") == 0) {
          vaddr = seg_cmd.vmaddr;
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
    return BinaryInfo{*build_id, vaddr, 0, type};
  }
  return {};
}

std::optional<BinaryInfo> GetBinaryInfo(const char* fname, size_t size) {
  static_assert(EI_CLASS > EI_MAG3, "mem[EI_MAG?] accesses are in range.");
  if (size <= EI_CLASS) {
    return std::nullopt;
  }
  base::ScopedMmap map = base::ReadMmapFilePart(fname, size);
  if (!map.IsValid()) {
    PERFETTO_PLOG("Failed to mmap %s", fname);
    return std::nullopt;
  }
  char* mem = static_cast<char*>(map.data());

  std::optional<std::string> build_id;
  std::optional<std::pair<uint64_t, uint64_t>> vaddr_and_offset;
  if (IsElf(mem, size)) {
    switch (mem[EI_CLASS]) {
      case ELFCLASS32:
        build_id = GetElfBuildId<Elf32>(mem, size);
        vaddr_and_offset = GetElfPVAddrPOffset<Elf32>(mem, size);
        break;
      case ELFCLASS64:
        build_id = GetElfBuildId<Elf64>(mem, size);
        vaddr_and_offset = GetElfPVAddrPOffset<Elf64>(mem, size);
        break;
      default:
        return std::nullopt;
    }
    if (build_id && vaddr_and_offset) {
      return BinaryInfo{
          *build_id,
          vaddr_and_offset->first,
          vaddr_and_offset->second,
          BinaryType::kElf,
      };
    }
  } else if (IsMachO64(mem, size)) {
    return GetMachOBinaryInfo(mem, size);
  }
  return std::nullopt;
}

// Helper function to process a single binary file and add it to the index
void ProcessBinaryFile(const char* fname,
                       size_t size,
                       std::map<std::string, FoundBinary>& result) {
  static_assert(EI_MAG3 + 1 == sizeof(kMachO64Magic));
  char magic[EI_MAG3 + 1];
  // Scope file access. On windows OpenFile opens an exclusive lock.
  // This lock needs to be released before mapping the file.
  {
    base::ScopedFile fd(base::OpenFile(fname, O_RDONLY));
    if (!fd) {
      PERFETTO_PLOG("Failed to open %s", fname);
      return;
    }
    auto rd = base::Read(*fd, &magic, sizeof(magic));
    if (rd != sizeof(magic) || (!IsElf(magic, static_cast<size_t>(rd)) &&
                                !IsMachO64(magic, static_cast<size_t>(rd)))) {
      PERFETTO_DLOG("%s not an ELF or Mach-O 64.", fname);
      return;
    }
  }
  std::optional<BinaryInfo> binary_info = GetBinaryInfo(fname, size);
  if (!binary_info) {
    PERFETTO_DLOG("Failed to extract build id from %s.", fname);
    return;
  }
  auto [it, inserted] =
      result.emplace(binary_info->build_id, FoundBinary{
                                                fname,
                                                binary_info->p_vaddr,
                                                binary_info->p_offset,
                                                binary_info->type,
                                            });

  if (inserted) {
    PERFETTO_DLOG("Indexed: %s (%s)", fname,
                  base::ToHex(binary_info->build_id).c_str());
    return;
  }

  // If there was already an existing FoundBinary, the emplace wouldn't insert
  // anything. But, for Mac binaries, we prefer dSYM files over the original
  // binary, so make sure these overwrite the FoundBinary entry.
  if (it->second.type == BinaryType::kMachO &&
      binary_info->type == BinaryType::kMachODsym) {
    PERFETTO_LOG("Overwriting index entry for %s to %s.",
                 base::ToHex(binary_info->build_id).c_str(), fname);
    it->second = FoundBinary{fname, binary_info->p_vaddr, binary_info->p_offset,
                             binary_info->type};
  } else {
    PERFETTO_DLOG("Ignoring %s, index entry for %s already exists.", fname,
                  base::ToHex(binary_info->build_id).c_str());
  }
}

std::map<std::string, FoundBinary> BuildIdIndex(
    std::vector<std::string> dirs,
    std::vector<std::string> files) {
  std::map<std::string, FoundBinary> result;

  // Process directories
  if (!dirs.empty()) {
    WalkDirectories(std::move(dirs), [&result](const char* fname, size_t size) {
      ProcessBinaryFile(fname, size, result);
    });
  }

  // Process individual files
  for (const std::string& file_path : files) {
    ProcessBinaryFile(file_path.c_str(), 0, result);
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
    std::optional<std::string_view> build_id) {
  if (!base::FileExists(symbol_file)) {
    return std::nullopt;
  }
  // Openfile opens the file with an exclusive lock on windows.
  std::optional<uint64_t> file_size = base::GetFileSize(symbol_file);
  if (!file_size.has_value()) {
    PERFETTO_PLOG("Failed to get file size %s", symbol_file.c_str());
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
  if (build_id && binary_info->build_id != *build_id) {
    return std::nullopt;
  }
  return FoundBinary{symbol_file, binary_info->p_vaddr, binary_info->p_offset,
                     binary_info->type};
}

std::optional<FoundBinary> FindBinaryInRoot(const std::string& root_str,
                                            const std::string& abspath,
                                            const std::string& build_id) {
  constexpr char kApkPrefix[] = "base.apk!";

  std::string filename;
  std::string dirname;

  for (base::StringSplitter sp(abspath, '/'); sp.Next();) {
    if (!dirname.empty())
      dirname += "/";
    dirname += filename;
    filename = sp.cur_token();
  }

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

  std::optional<FoundBinary> result;

  std::string symbol_file = root_str + "/" + dirname + "/" + filename;
  result = IsCorrectFile(symbol_file, build_id);
  if (result)
    return result;

  if (base::StartsWith(filename, kApkPrefix)) {
    symbol_file = root_str + "/" + dirname + "/" +
                  filename.substr(sizeof(kApkPrefix) - 1);
    result = IsCorrectFile(symbol_file, build_id);
    if (result)
      return result;
  }

  symbol_file = root_str + "/" + filename;
  result = IsCorrectFile(symbol_file, build_id);
  if (result)
    return result;

  if (base::StartsWith(filename, kApkPrefix)) {
    symbol_file = root_str + "/" + filename.substr(sizeof(kApkPrefix) - 1);
    result = IsCorrectFile(symbol_file, build_id);
    if (result)
      return result;
  }

  std::string hex_build_id = base::ToHex(build_id.c_str(), build_id.size());
  std::string split_hex_build_id = SplitBuildID(hex_build_id);
  if (!split_hex_build_id.empty()) {
    symbol_file =
        root_str + "/" + ".build-id" + "/" + split_hex_build_id + ".debug";
    result = IsCorrectFile(symbol_file, build_id);
    if (result)
      return result;
  }

  return std::nullopt;
}

std::optional<FoundBinary> FindKernelBinary(const std::string& os_release) {
  using SS = base::StackString<512>;
  const char* rel = os_release.c_str();
  auto find_kernel = [](base::StackString<512> path) {
    return IsCorrectFile(path.ToStdString(), std::nullopt);
  };
  // This list comes from the perf symbolization code [1]: it's an incomplete
  // list (it doesn't include pre-symbolized kernels or reading /proc/kallsyms)
  // but works if you just install e.g. the symbol packages for the kernel.
  //
  // [1]
  // https://elixir.bootlin.com/linux/v6.12.2/source/tools/perf/util/symbol.c#L2294
  if (auto b = find_kernel(SS("/boot/vmlinux-%s", rel))) {
    return b;
  }
  if (auto b = find_kernel(SS("/usr/lib/debug/boot/vmlinux-%s", rel))) {
    return b;
  }
  if (auto b = find_kernel(SS("/lib/modules/%s/build/vmlinux", rel))) {
    return b;
  }
  if (auto b = find_kernel(SS("/usr/lib/debug/lib/modules/%s/vmlinux", rel))) {
    return b;
  }
  if (auto b = find_kernel(SS("/usr/lib/debug/boot/vmlinux-%s.debug", rel))) {
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
    : buildid_to_file_(
          BuildIdIndex(std::move(directories), std::move(individual_files))) {}

std::optional<FoundBinary> LocalBinaryIndexer::FindBinary(
    const std::string& abspath,
    const std::string& build_id) {
  auto it = buildid_to_file_.find(build_id);
  if (it != buildid_to_file_.end()) {
    return it->second;
  }
  PERFETTO_ELOG("Could not find Build ID: %s (file %s).",
                base::ToHex(build_id).c_str(), abspath.c_str());
  return std::nullopt;
}

LocalBinaryIndexer::~LocalBinaryIndexer() = default;

LocalBinaryFinder::LocalBinaryFinder(std::vector<std::string> roots)
    : roots_(std::move(roots)) {}

std::optional<FoundBinary> LocalBinaryFinder::FindBinary(
    const std::string& abspath,
    const std::string& build_id) {
  auto p = cache_.emplace(abspath, std::nullopt);
  if (!p.second)
    return p.first->second;

  std::optional<FoundBinary>& cache_entry = p.first->second;

  // Try the absolute path first.
  if (base::StartsWith(abspath, "/")) {
    cache_entry = IsCorrectFile(abspath, build_id);
    if (cache_entry)
      return cache_entry;
  }

  for (const std::string& root_str : roots_) {
    cache_entry = FindBinaryInRoot(root_str, abspath, build_id);
    if (cache_entry)
      return cache_entry;
  }
  PERFETTO_ELOG("Could not find %s (Build ID: %s).", abspath.c_str(),
                base::ToHex(build_id).c_str());
  return cache_entry;
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

std::vector<std::vector<SymbolizedFrame>> LocalSymbolizer::Symbolize(
    const Environment& env,
    const std::string& mapping_name,
    const std::string& build_id,
    uint64_t load_bias,
    const std::vector<uint64_t>& addresses) {
  bool is_kernel = base::StartsWith(mapping_name, "[kernel.kallsyms]");
  std::optional<FoundBinary> binary;
  if (is_kernel) {
    if (env.os_release) {
      binary = FindKernelBinary(*env.os_release);
    }
  } else {
    binary = finder_->FindBinary(mapping_name, build_id);
  }
  if (!binary) {
    return {};
  }
  uint64_t binary_load_bias = binary->p_vaddr - binary->p_offset;
  uint64_t addr_correction = 0;
  if (is_kernel) {
    // We expect this branch to be hit when symbolizing kernel frames with Linux
    // perf (*not* simpleperf). In that case, we need to add the vaddr
    // because llvm-symbolizer expects that we provide absolute addresses unlike
    // all other files where it expects relative addresses.
    addr_correction = binary->p_vaddr;
  } else if (binary->p_offset > 0 && binary_load_bias > load_bias) {
    // On Android 10, there was a bug in libunwindstack that would incorrectly
    // calculate the load_bias, and thus the relative PC. This would end up in
    // frames that made no sense. We can fix this up after the fact if we
    // detect this situation.
    //
    // Note that the `binary->p_offset > 0` check above accounts for perf.data
    // files: in those, load_bias from the trace is always zero but we should
    // *not* enter this codepath. Thankfully, in those cases `p_offset` is zero:
    // symbol elfs always seem to have the text segment's `p_offset` zeroed out.
    // Whereas with libunwindstack, `p_offset` should always be greater than
    // zero.
    addr_correction = (binary->p_vaddr - binary->p_offset) - load_bias;
    PERFETTO_LOG("Correcting load bias by %" PRIu64 " for %s", addr_correction,
                 mapping_name.c_str());
  }
  std::vector<std::vector<SymbolizedFrame>> result;
  result.reserve(addresses.size());
  for (uint64_t address : addresses) {
    result.emplace_back(llvm_symbolizer_.Symbolize(binary->file_name,
                                                   address + addr_correction));
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
