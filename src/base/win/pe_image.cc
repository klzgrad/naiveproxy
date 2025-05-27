// Copyright 2010 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

// This file implements PEImage, a generic class to manipulate PE files.
// This file was adapted from GreenBorder's Code.

#include "base/win/pe_image.h"

#include <delayimp.h>
#include <stddef.h>

#include <set>
#include <string>

#include "base/no_destructor.h"
#include "base/win/current_module.h"

namespace base {
namespace win {

// Structure to perform imports enumerations.
struct EnumAllImportsStorage {
  PEImage::EnumImportsFunction callback;
  PVOID cookie;
};

namespace {

// PdbInfo Signature
const DWORD kPdbInfoSignature = 'SDSR';

// Compare two strings byte by byte on an unsigned basis.
//   if s1 == s2, return 0
//   if s1 < s2, return negative
//   if s1 > s2, return positive
// Exception if inputs are invalid.
int StrCmpByByte(LPCSTR s1, LPCSTR s2) {
  while (*s1 != '\0' && *s1 == *s2) {
    ++s1;
    ++s2;
  }

  return (*reinterpret_cast<const unsigned char*>(s1) -
          *reinterpret_cast<const unsigned char*>(s2));
}

struct PdbInfo {
  DWORD Signature;
  GUID Guid;
  DWORD Age;
  char PdbFileName[1];
};

#define LDR_IS_DATAFILE(handle) (((ULONG_PTR)(handle)) & (ULONG_PTR)1)
#define LDR_IS_IMAGEMAPPING(handle) (((ULONG_PTR)(handle)) & (ULONG_PTR)2)
#define LDR_IS_RESOURCE(handle) \
  (LDR_IS_IMAGEMAPPING(handle) || LDR_IS_DATAFILE(handle))

}  // namespace

// Callback used to enumerate imports. See EnumImportChunksFunction.
bool ProcessImportChunk(const PEImage& image,
                        LPCSTR module,
                        PIMAGE_THUNK_DATA name_table,
                        PIMAGE_THUNK_DATA iat,
                        PVOID cookie) {
  EnumAllImportsStorage& storage =
      *reinterpret_cast<EnumAllImportsStorage*>(cookie);

  return image.EnumOneImportChunk(storage.callback, module, name_table, iat,
                                  storage.cookie);
}

// Callback used to enumerate delay imports. See EnumDelayImportChunksFunction.
bool ProcessDelayImportChunk(const PEImage& image,
                             PImgDelayDescr delay_descriptor,
                             LPCSTR module,
                             PIMAGE_THUNK_DATA name_table,
                             PIMAGE_THUNK_DATA iat,
                             PVOID cookie) {
  EnumAllImportsStorage& storage =
      *reinterpret_cast<EnumAllImportsStorage*>(cookie);

  return image.EnumOneDelayImportChunk(storage.callback, delay_descriptor,
                                       module, name_table, iat, storage.cookie);
}

void PEImage::set_module(HMODULE module) {
  module_ = module;
}

PIMAGE_DOS_HEADER PEImage::GetDosHeader() const {
  return reinterpret_cast<PIMAGE_DOS_HEADER>(module_);
}

PIMAGE_NT_HEADERS PEImage::GetNTHeaders() const {
  PIMAGE_DOS_HEADER dos_header = GetDosHeader();

  return reinterpret_cast<PIMAGE_NT_HEADERS>(
      reinterpret_cast<char*>(dos_header) + dos_header->e_lfanew);
}

PIMAGE_SECTION_HEADER PEImage::GetSectionHeader(WORD section) const {
  PIMAGE_NT_HEADERS nt_headers = GetNTHeaders();
  PIMAGE_SECTION_HEADER first_section = IMAGE_FIRST_SECTION(nt_headers);

  if (section < nt_headers->FileHeader.NumberOfSections) {
    return first_section + section;
  } else {
    return nullptr;
  }
}

WORD PEImage::GetNumSections() const {
  return GetNTHeaders()->FileHeader.NumberOfSections;
}

DWORD PEImage::GetImageDirectoryEntrySize(UINT directory) const {
  const IMAGE_DATA_DIRECTORY* const entry = GetDataDirectory(directory);
  return entry ? entry->Size : 0;
}

PVOID PEImage::GetImageDirectoryEntryAddr(UINT directory) const {
  const IMAGE_DATA_DIRECTORY* const entry = GetDataDirectory(directory);
  return entry ? RVAToAddr(entry->VirtualAddress) : nullptr;
}

PIMAGE_SECTION_HEADER PEImage::GetImageSectionFromAddr(PVOID address) const {
  PBYTE target = reinterpret_cast<PBYTE>(address);
  PIMAGE_SECTION_HEADER section;

  for (WORD i = 0; nullptr != (section = GetSectionHeader(i)); i++) {
    // Don't use the virtual RVAToAddr.
    PBYTE start =
        reinterpret_cast<PBYTE>(PEImage::RVAToAddr(section->VirtualAddress));

    DWORD size = section->Misc.VirtualSize;

    if ((start <= target) && (start + size > target)) {
      return section;
    }
  }

  return nullptr;
}

PIMAGE_SECTION_HEADER PEImage::GetImageSectionHeaderByName(
    LPCSTR section_name) const {
  if (section_name == nullptr) {
    return nullptr;
  }

  WORD num_sections = GetNumSections();
  for (WORD i = 0; i < num_sections; ++i) {
    PIMAGE_SECTION_HEADER section = GetSectionHeader(i);
    if (_strnicmp(reinterpret_cast<LPCSTR>(section->Name), section_name,
                  sizeof(section->Name)) == 0) {
      return section;
    }
  }

  return nullptr;
}

bool PEImage::GetDebugId(LPGUID guid,
                         LPDWORD age,
                         LPCSTR* pdb_filename,
                         size_t* pdb_filename_length) const {
  DWORD debug_directory_size =
      GetImageDirectoryEntrySize(IMAGE_DIRECTORY_ENTRY_DEBUG);
  PIMAGE_DEBUG_DIRECTORY debug_directory =
      reinterpret_cast<PIMAGE_DEBUG_DIRECTORY>(
          GetImageDirectoryEntryAddr(IMAGE_DIRECTORY_ENTRY_DEBUG));
  if (!debug_directory) {
    return false;
  }

  size_t directory_count = debug_directory_size / sizeof(IMAGE_DEBUG_DIRECTORY);
  for (size_t index = 0; index < directory_count; ++index) {
    const IMAGE_DEBUG_DIRECTORY& entry = debug_directory[index];
    if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW) {
      continue;  // Unsupported debugging info format.
    }
    if (entry.SizeOfData < sizeof(PdbInfo)) {
      continue;  // The data is too small to hold PDB info.
    }
    const PdbInfo* pdb_info =
        reinterpret_cast<const PdbInfo*>(RVAToAddr(entry.AddressOfRawData));
    if (!pdb_info) {
      continue;  // The data is not present in a mapped section.
    }
    if (pdb_info->Signature != kPdbInfoSignature) {
      continue;  // Unsupported PdbInfo signature
    }

    if (guid) {
      *guid = pdb_info->Guid;
    }
    if (age) {
      *age = pdb_info->Age;
    }
    if (pdb_filename) {
      const size_t length_max =
          entry.SizeOfData - offsetof(PdbInfo, PdbFileName);
      const char* eos = pdb_info->PdbFileName;
      for (const char* const end = pdb_info->PdbFileName + length_max;
           eos < end && *eos; ++eos)
        ;
      // This static_cast is safe because the loop above only increments eos,
      // and ensures it won't wrap.
      *pdb_filename_length = static_cast<size_t>(eos - pdb_info->PdbFileName);
      *pdb_filename = pdb_info->PdbFileName;
    }
    return true;
  }
  return false;
}

PDWORD PEImage::GetExportEntry(LPCSTR name) const {
  PIMAGE_EXPORT_DIRECTORY exports = GetExportDirectory();

  if (nullptr == exports) {
    return nullptr;
  }

  WORD ordinal = 0;
  if (!GetProcOrdinal(name, &ordinal)) {
    return nullptr;
  }

  PDWORD functions =
      reinterpret_cast<PDWORD>(RVAToAddr(exports->AddressOfFunctions));

  return functions + ordinal - exports->Base;
}

FARPROC PEImage::GetProcAddress(LPCSTR function_name) const {
  PDWORD export_entry = GetExportEntry(function_name);
  if (nullptr == export_entry) {
    return nullptr;
  }

  PBYTE function = reinterpret_cast<PBYTE>(RVAToAddr(*export_entry));

  PBYTE exports = reinterpret_cast<PBYTE>(
      GetImageDirectoryEntryAddr(IMAGE_DIRECTORY_ENTRY_EXPORT));
  DWORD size = GetImageDirectoryEntrySize(IMAGE_DIRECTORY_ENTRY_EXPORT);
  if (!exports || !size) {
    return nullptr;
  }

  // Check for forwarded exports as a special case.
  if (exports <= function && exports + size > function) {
    return reinterpret_cast<FARPROC>(-1);
  }

  return reinterpret_cast<FARPROC>(function);
}

bool PEImage::GetProcOrdinal(LPCSTR function_name, WORD* ordinal) const {
  if (nullptr == ordinal) {
    return false;
  }

  PIMAGE_EXPORT_DIRECTORY exports = GetExportDirectory();

  if (nullptr == exports) {
    return false;
  }

  if (IsOrdinal(function_name)) {
    *ordinal = ToOrdinal(function_name);
  } else {
    PDWORD names = reinterpret_cast<PDWORD>(RVAToAddr(exports->AddressOfNames));
    PDWORD lower = names;
    PDWORD upper = names + exports->NumberOfNames;
    int cmp = -1;

    // Binary Search for the name.
    while (lower != upper) {
      PDWORD middle = lower + (upper - lower) / 2;
      LPCSTR name = reinterpret_cast<LPCSTR>(RVAToAddr(*middle));

      // This may be called by sandbox before MSVCRT dll loads, so can't use
      // CRT function here.
      cmp = StrCmpByByte(function_name, name);

      if (cmp == 0) {
        lower = middle;
        break;
      }

      if (cmp > 0) {
        lower = middle + 1;
      } else {
        upper = middle;
      }
    }

    if (cmp != 0) {
      return false;
    }

    PWORD ordinals =
        reinterpret_cast<PWORD>(RVAToAddr(exports->AddressOfNameOrdinals));

    *ordinal = ordinals[lower - names] + static_cast<WORD>(exports->Base);
  }

  return true;
}

bool PEImage::EnumSections(EnumSectionsFunction callback, PVOID cookie) const {
  PIMAGE_NT_HEADERS nt_headers = GetNTHeaders();
  UINT num_sections = nt_headers->FileHeader.NumberOfSections;
  PIMAGE_SECTION_HEADER section = GetSectionHeader(0);

  for (WORD i = 0; i < num_sections; i++, section++) {
    PVOID section_start = RVAToAddr(section->VirtualAddress);
    DWORD size = section->Misc.VirtualSize;

    if (!callback(*this, section, section_start, size, cookie)) {
      return false;
    }
  }

  return true;
}

bool PEImage::EnumExports(EnumExportsFunction callback, PVOID cookie) const {
  PVOID directory = GetImageDirectoryEntryAddr(IMAGE_DIRECTORY_ENTRY_EXPORT);
  DWORD size = GetImageDirectoryEntrySize(IMAGE_DIRECTORY_ENTRY_EXPORT);

  // Check if there are any exports at all.
  if (!directory || !size) {
    return true;
  }

  PIMAGE_EXPORT_DIRECTORY exports =
      reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(directory);
  UINT ordinal_base = exports->Base;
  UINT num_funcs = exports->NumberOfFunctions;
  UINT num_names = exports->NumberOfNames;
  PDWORD functions =
      reinterpret_cast<PDWORD>(RVAToAddr(exports->AddressOfFunctions));
  PDWORD names = reinterpret_cast<PDWORD>(RVAToAddr(exports->AddressOfNames));
  PWORD ordinals =
      reinterpret_cast<PWORD>(RVAToAddr(exports->AddressOfNameOrdinals));

  for (UINT count = 0; count < num_funcs; count++) {
    PVOID func = RVAToAddr(functions[count]);
    if (nullptr == func) {
      continue;
    }

    // Check for a name.
    LPCSTR name = nullptr;
    UINT hint;
    for (hint = 0; hint < num_names; hint++) {
      if (ordinals[hint] == count) {
        name = reinterpret_cast<LPCSTR>(RVAToAddr(names[hint]));
        break;
      }
    }

    if (name == nullptr) {
      hint = 0;
    }

    // Check for forwarded exports.
    LPCSTR forward = nullptr;
    if (reinterpret_cast<char*>(func) >= reinterpret_cast<char*>(directory) &&
        reinterpret_cast<char*>(func) <=
            reinterpret_cast<char*>(directory) + size) {
      forward = reinterpret_cast<LPCSTR>(func);
      func = nullptr;
    }

    if (!callback(*this, ordinal_base + count, hint, name, func, forward,
                  cookie)) {
      return false;
    }
  }

  return true;
}

bool PEImage::EnumRelocs(EnumRelocsFunction callback, PVOID cookie) const {
  PVOID directory = GetImageDirectoryEntryAddr(IMAGE_DIRECTORY_ENTRY_BASERELOC);
  DWORD size = GetImageDirectoryEntrySize(IMAGE_DIRECTORY_ENTRY_BASERELOC);

  if (!directory || !size) {
    return true;
  }

  PIMAGE_BASE_RELOCATION base =
      reinterpret_cast<PIMAGE_BASE_RELOCATION>(directory);
  while (size >= sizeof(IMAGE_BASE_RELOCATION) && base->SizeOfBlock &&
         size >= base->SizeOfBlock) {
    PWORD reloc = reinterpret_cast<PWORD>(base + 1);
    UINT num_relocs =
        (base->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);

    for (UINT i = 0; i < num_relocs; i++, reloc++) {
      WORD type = *reloc >> 12;
      PVOID address = RVAToAddr(base->VirtualAddress + (*reloc & 0x0FFF));

      if (!callback(*this, type, address, cookie)) {
        return false;
      }
    }

    size -= base->SizeOfBlock;
    base = reinterpret_cast<PIMAGE_BASE_RELOCATION>(
        reinterpret_cast<char*>(base) + base->SizeOfBlock);
  }

  return true;
}

bool PEImage::EnumImportChunks(EnumImportChunksFunction callback,
                               PVOID cookie,
                               LPCSTR target_module_name) const {
  DWORD size = GetImageDirectoryEntrySize(IMAGE_DIRECTORY_ENTRY_IMPORT);
  PIMAGE_IMPORT_DESCRIPTOR import = GetFirstImportChunk();

  if (import == nullptr || size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    return true;
  }

  for (; import->FirstThunk; import++) {
    LPCSTR module_name = reinterpret_cast<LPCSTR>(RVAToAddr(import->Name));
    PIMAGE_THUNK_DATA name_table = reinterpret_cast<PIMAGE_THUNK_DATA>(
        RVAToAddr(import->OriginalFirstThunk));
    PIMAGE_THUNK_DATA iat =
        reinterpret_cast<PIMAGE_THUNK_DATA>(RVAToAddr(import->FirstThunk));

    if (target_module_name == nullptr ||
        (lstrcmpiA(module_name, target_module_name) == 0)) {
      if (!callback(*this, module_name, name_table, iat, cookie)) {
        return false;
      }
    }
  }

  return true;
}

bool PEImage::EnumOneImportChunk(EnumImportsFunction callback,
                                 LPCSTR module_name,
                                 PIMAGE_THUNK_DATA name_table,
                                 PIMAGE_THUNK_DATA iat,
                                 PVOID cookie) const {
  if (nullptr == name_table) {
    return false;
  }

  for (; name_table && name_table->u1.Ordinal; name_table++, iat++) {
    LPCSTR name = nullptr;
    WORD ordinal = 0;
    WORD hint = 0;

    if (IMAGE_SNAP_BY_ORDINAL(name_table->u1.Ordinal)) {
      ordinal = static_cast<WORD>(IMAGE_ORDINAL32(name_table->u1.Ordinal));
    } else {
      PIMAGE_IMPORT_BY_NAME import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
          RVAToAddr(name_table->u1.ForwarderString));

      hint = import->Hint;
      name = reinterpret_cast<LPCSTR>(&import->Name);
    }

    if (!callback(*this, module_name, ordinal, name, hint, iat, cookie)) {
      return false;
    }
  }

  return true;
}

bool PEImage::EnumAllImports(EnumImportsFunction callback,
                             PVOID cookie,
                             LPCSTR target_module_name) const {
  EnumAllImportsStorage temp = {callback, cookie};
  return EnumImportChunks(ProcessImportChunk, &temp, target_module_name);
}

bool PEImage::EnumDelayImportChunks(EnumDelayImportChunksFunction callback,
                                    PVOID cookie,
                                    LPCSTR target_module_name) const {
  PVOID directory =
      GetImageDirectoryEntryAddr(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
  DWORD size = GetImageDirectoryEntrySize(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);

  if (!directory || !size) {
    return true;
  }

  PImgDelayDescr delay_descriptor = reinterpret_cast<PImgDelayDescr>(directory);
  for (; delay_descriptor->rvaHmod; delay_descriptor++) {
    PIMAGE_THUNK_DATA name_table;
    PIMAGE_THUNK_DATA iat;
    LPCSTR module_name;

    // check if VC7-style imports, using RVAs instead of
    // VC6-style addresses.
    bool rvas = (delay_descriptor->grAttrs & dlattrRva) != 0;

    if (rvas) {
      module_name =
          reinterpret_cast<LPCSTR>(RVAToAddr(delay_descriptor->rvaDLLName));
      name_table = reinterpret_cast<PIMAGE_THUNK_DATA>(
          RVAToAddr(delay_descriptor->rvaINT));
      iat = reinterpret_cast<PIMAGE_THUNK_DATA>(
          RVAToAddr(delay_descriptor->rvaIAT));
    } else {
      // Values in IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT are 32-bit, even on 64-bit
      // platforms. See section 4.8 of PECOFF image spec rev 8.3.
      module_name = reinterpret_cast<LPCSTR>(
          static_cast<uintptr_t>(delay_descriptor->rvaDLLName));
      name_table = reinterpret_cast<PIMAGE_THUNK_DATA>(
          static_cast<uintptr_t>(delay_descriptor->rvaINT));
      iat = reinterpret_cast<PIMAGE_THUNK_DATA>(
          static_cast<uintptr_t>(delay_descriptor->rvaIAT));
    }

    if (target_module_name == nullptr ||
        (lstrcmpiA(module_name, target_module_name) == 0)) {
      if (target_module_name) {
        // Ensure all imports are properly loaded for the target module so that
        // the callback is operating on a fully-realized set of imports.
        // This call only loads the imports for the module where this code is
        // executing, so it is only helpful or meaningful to do this if the
        // current module is the module whose IAT we are enumerating.
        // Use the module_name as retrieved from the IAT because this method
        // is case sensitive.
        if (module_ == CURRENT_MODULE() && !LDR_IS_RESOURCE(module_)) {
          static base::NoDestructor<std::set<std::string>> loaded_dlls;
          // pair.second is true if this is a new element
          if (loaded_dlls.get()->emplace(module_name).second) {
            ::__HrLoadAllImportsForDll(module_name);
          }
        }
      }

      if (!callback(*this, delay_descriptor, module_name, name_table, iat,
                    cookie)) {
        return false;
      }
    }
  }

  return true;
}

bool PEImage::EnumOneDelayImportChunk(EnumImportsFunction callback,
                                      PImgDelayDescr delay_descriptor,
                                      LPCSTR module_name,
                                      PIMAGE_THUNK_DATA name_table,
                                      PIMAGE_THUNK_DATA iat,
                                      PVOID cookie) const {
  for (; name_table->u1.Ordinal; name_table++, iat++) {
    LPCSTR name = nullptr;
    WORD ordinal = 0;
    WORD hint = 0;

    if (IMAGE_SNAP_BY_ORDINAL(name_table->u1.Ordinal)) {
      ordinal = static_cast<WORD>(IMAGE_ORDINAL32(name_table->u1.Ordinal));
    } else {
      PIMAGE_IMPORT_BY_NAME import;
      bool rvas = (delay_descriptor->grAttrs & dlattrRva) != 0;

      if (rvas) {
        import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
            RVAToAddr(name_table->u1.ForwarderString));
      } else {
        import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
            name_table->u1.ForwarderString);
      }

      hint = import->Hint;
      name = reinterpret_cast<LPCSTR>(&import->Name);
    }

    if (!callback(*this, module_name, ordinal, name, hint, iat, cookie)) {
      return false;
    }
  }

  return true;
}

bool PEImage::EnumAllDelayImports(EnumImportsFunction callback,
                                  PVOID cookie,
                                  LPCSTR target_module_name) const {
  EnumAllImportsStorage temp = {callback, cookie};
  return EnumDelayImportChunks(ProcessDelayImportChunk, &temp,
                               target_module_name);
}

bool PEImage::VerifyMagic() const {
  PIMAGE_DOS_HEADER dos_header = GetDosHeader();

  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    return false;
  }

  PIMAGE_NT_HEADERS nt_headers = GetNTHeaders();

  if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
    return false;
  }

  if (nt_headers->FileHeader.SizeOfOptionalHeader !=
      sizeof(IMAGE_OPTIONAL_HEADER)) {
    return false;
  }

  if (nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) {
    return false;
  }

  return true;
}

bool PEImage::ImageRVAToOnDiskOffset(uintptr_t rva,
                                     DWORD* on_disk_offset) const {
  LPVOID address = RVAToAddr(rva);
  return ImageAddrToOnDiskOffset(address, on_disk_offset);
}

bool PEImage::ImageAddrToOnDiskOffset(LPVOID address,
                                      DWORD* on_disk_offset) const {
  if (nullptr == address) {
    return false;
  }

  // Get the section that this address belongs to.
  PIMAGE_SECTION_HEADER section_header = GetImageSectionFromAddr(address);
  if (nullptr == section_header) {
    return false;
  }

  // Don't follow the virtual RVAToAddr, use the one on the base.
  DWORD offset_within_section =
      static_cast<DWORD>(reinterpret_cast<uintptr_t>(address)) -
      static_cast<DWORD>(reinterpret_cast<uintptr_t>(
          PEImage::RVAToAddr(section_header->VirtualAddress)));

  *on_disk_offset = section_header->PointerToRawData + offset_within_section;
  return true;
}

PVOID PEImage::RVAToAddr(uintptr_t rva) const {
  if (rva == 0) {
    return nullptr;
  }

  return reinterpret_cast<char*>(module_) + rva;
}

const IMAGE_DATA_DIRECTORY* PEImage::GetDataDirectory(UINT directory) const {
  PIMAGE_NT_HEADERS nt_headers = GetNTHeaders();

  // Does the image report that it includes this directory entry?
  if (directory >= nt_headers->OptionalHeader.NumberOfRvaAndSizes) {
    return nullptr;
  }

  // Is there space for this directory entry in the optional header?
  if (nt_headers->FileHeader.SizeOfOptionalHeader <
      (offsetof(IMAGE_OPTIONAL_HEADER, DataDirectory) +
       (directory + 1) * sizeof(IMAGE_DATA_DIRECTORY))) {
    return nullptr;
  }

  return &nt_headers->OptionalHeader.DataDirectory[directory];
}

PVOID PEImageAsData::RVAToAddr(uintptr_t rva) const {
  if (rva == 0) {
    return nullptr;
  }

  PVOID in_memory = PEImage::RVAToAddr(rva);
  DWORD disk_offset;

  if (!ImageAddrToOnDiskOffset(in_memory, &disk_offset)) {
    return nullptr;
  }

  return PEImage::RVAToAddr(disk_offset);
}

}  // namespace win
}  // namespace base
