// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the version of the Android-specific Chromium linker that uses
// the Android M and later system linker to load libraries.

// This source code *cannot* depend on anything from base/ or the C++
// STL, to keep the final library small, and avoid ugly dependency issues.

#include "modern_linker_jni.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <limits.h>
#include <link.h>
#include <stddef.h>
#include <string.h>

#include "android_dlext.h"
#include "linker_jni.h"

#define PAGE_START(x) ((x) & PAGE_MASK)
#define PAGE_END(x) PAGE_START((x) + (PAGE_SIZE - 1))

namespace chromium_android_linker {
namespace {

// Record of the Java VM passed to JNI_OnLoad().
static JavaVM* s_java_vm = nullptr;

// Get the CPU ABI string for which the linker is running.
//
// The returned string is used to construct the path to libchrome.so when
// loading directly from APK.
//
// |env| is the current JNI environment handle.
// |clazz| is the static class handle for org.chromium.base.Linker,
// and is ignored here.
// Returns the CPU ABI string for which the linker is running.
jstring GetCpuAbi(JNIEnv* env, jclass clazz) {
#if defined(__arm__) && defined(__ARM_ARCH_7A__)
  static const char* kCurrentAbi = "armeabi-v7a";
#elif defined(__arm__)
  static const char* kCurrentAbi = "armeabi";
#elif defined(__i386__)
  static const char* kCurrentAbi = "x86";
#elif defined(__mips__)
  static const char* kCurrentAbi = "mips";
#elif defined(__x86_64__)
  static const char* kCurrentAbi = "x86_64";
#elif defined(__aarch64__)
  static const char* kCurrentAbi = "arm64-v8a";
#else
#error "Unsupported target abi"
#endif
  return env->NewStringUTF(kCurrentAbi);
}

// Convenience wrapper around dlsym() on the main executable. Returns
// the address of the requested symbol, or nullptr if not found. Status
// is available from dlerror().
void* Dlsym(const char* symbol_name) {
  static void* handle = nullptr;

  if (!handle)
    handle = dlopen(nullptr, RTLD_NOW);

  void* result = dlsym(handle, symbol_name);
  return result;
}

// dl_iterate_phdr() wrapper, accessed via dlsym lookup. Done this way.
// so that this code compiles for Android versions that are too early to
// offer it. Checks in LibraryLoader.java should ensure that we
// never reach here at runtime on Android versions that are too old to
// supply dl_iterate_phdr; that is, earlier than Android M. Returns
// false if no dl_iterate_phdr() is available, otherwise true with the
// return value from dl_iterate_phdr() in |status|.
bool DlIteratePhdr(int (*callback)(dl_phdr_info*, size_t, void*),
                   void* data,
                   int* status) {
  using DlIteratePhdrCallback = int (*)(dl_phdr_info*, size_t, void*);
  using DlIteratePhdrFunctionPtr = int (*)(DlIteratePhdrCallback, void*);
  static DlIteratePhdrFunctionPtr function_ptr = nullptr;

  if (!function_ptr) {
    function_ptr =
        reinterpret_cast<DlIteratePhdrFunctionPtr>(Dlsym("dl_iterate_phdr"));
    if (!function_ptr) {
      LOG_ERROR("dlsym: dl_iterate_phdr: %s", dlerror());
      return false;
    }
  }

  *status = (*function_ptr)(callback, data);
  return true;
}

// Convenience struct wrapper round android_dlextinfo.
struct AndroidDlextinfo {
  AndroidDlextinfo(int flags,
                   void* reserved_addr, size_t reserved_size, int relro_fd) {
    memset(&extinfo, 0, sizeof(extinfo));
    extinfo.flags = flags;
    extinfo.reserved_addr = reserved_addr;
    extinfo.reserved_size = reserved_size;
    extinfo.relro_fd = relro_fd;
  }

  android_dlextinfo extinfo;
};

// android_dlopen_ext() wrapper, accessed via dlsym lookup. Returns false
// if no android_dlopen_ext() is available, otherwise true with the return
// value from android_dlopen_ext() in |status|.
bool AndroidDlopenExt(const char* filename,
                      int flag,
                      const AndroidDlextinfo* dlextinfo,
                      void** status) {
  using DlopenExtFunctionPtr = void* (*)(const char*,
                                         int, const android_dlextinfo*);
  static DlopenExtFunctionPtr function_ptr = nullptr;

  if (!function_ptr) {
    function_ptr =
        reinterpret_cast<DlopenExtFunctionPtr>(Dlsym("android_dlopen_ext"));
    if (!function_ptr) {
      LOG_ERROR("dlsym: android_dlopen_ext: %s", dlerror());
      return false;
    }
  }

  const android_dlextinfo* extinfo = &dlextinfo->extinfo;
  LOG_INFO("android_dlopen_ext:"
           " flags=0x%llx, reserved_addr=%p, reserved_size=%d, relro_fd=%d",
           static_cast<long long>(extinfo->flags),
           extinfo->reserved_addr,
           static_cast<int>(extinfo->reserved_size),
           extinfo->relro_fd);

  *status = (*function_ptr)(filename, flag, extinfo);
  return true;
}

// Callback data for FindLoadedLibrarySize().
struct CallbackData {
  explicit CallbackData(void* address)
      : load_address(address), load_size(0), min_vaddr(0) { }

  const void* load_address;
  size_t load_size;
  size_t min_vaddr;
};

// Callback for dl_iterate_phdr(). Read phdrs to identify whether or not
// this library's load address matches the |load_address| passed in
// |data|. If yes, pass back load size and min vaddr via |data|. A non-zero
// return value terminates iteration.
int FindLoadedLibrarySize(dl_phdr_info* info, size_t size UNUSED, void* data) {
  CallbackData* callback_data = reinterpret_cast<CallbackData*>(data);

  // Use max and min vaddr to compute the library's load size.
  ElfW(Addr) min_vaddr = ~0;
  ElfW(Addr) max_vaddr = 0;

  bool is_matching = false;
  for (size_t i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr)* phdr = &info->dlpi_phdr[i];
    if (phdr->p_type != PT_LOAD)
      continue;

    // See if this segment's load address matches what we passed to
    // android_dlopen_ext as extinfo.reserved_addr.
    void* load_addr = reinterpret_cast<void*>(info->dlpi_addr + phdr->p_vaddr);
    if (load_addr == callback_data->load_address)
      is_matching = true;

    if (phdr->p_vaddr < min_vaddr)
      min_vaddr = phdr->p_vaddr;
    if (phdr->p_vaddr + phdr->p_memsz > max_vaddr)
      max_vaddr = phdr->p_vaddr + phdr->p_memsz;
  }

  // If this library matches what we seek, return its load size.
  if (is_matching) {
    callback_data->load_size = PAGE_END(max_vaddr) - PAGE_START(min_vaddr);
    callback_data->min_vaddr = min_vaddr;
    return true;
  }

  return false;
}

// Helper class for anonymous memory mapping.
class ScopedAnonymousMmap {
 public:
  ScopedAnonymousMmap(void* addr, size_t size);

  ~ScopedAnonymousMmap() { munmap(addr_, size_); }

  void* GetAddr() const { return effective_addr_; }
  void Release() { addr_ = nullptr; size_ = 0; effective_addr_ = nullptr; }

 private:
  void* addr_;
  size_t size_;

  // The effective_addr_ is the address seen by client code. It may or may
  // not be the same as addr_, the real start of the anonymous mapping.
  void* effective_addr_;
};

// ScopedAnonymousMmap constructor. |addr| is a requested mapping address, or
// zero if any address will do, and |size| is the size of mapping required.
ScopedAnonymousMmap::ScopedAnonymousMmap(void* addr, size_t size) {
#if RESERVE_BREAKPAD_GUARD_REGION
  // Increase size to extend the address reservation mapping so that it will
  // also include a guard region from load_bias_ to start_addr. If loading
  // at a fixed address, move our requested address back by the guard region
  // size.
  size += kBreakpadGuardRegionBytes;
  if (addr) {
    if (addr < reinterpret_cast<void*>(kBreakpadGuardRegionBytes)) {
      LOG_ERROR("Fixed address %p is too low to accommodate Breakpad guard",
                addr);
      addr_ = MAP_FAILED;
      size_ = 0;
      return;
    }
    addr = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(addr) - kBreakpadGuardRegionBytes);
  }
  LOG_INFO("Added %d to size, for Breakpad guard",
           static_cast<int>(kBreakpadGuardRegionBytes));
#endif

  addr_ = mmap(addr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr_ != MAP_FAILED) {
    size_ = size;
  } else {
    LOG_INFO("mmap failed: %s", strerror(errno));
    size_ = 0;
  }
  effective_addr_ = addr_;

#if RESERVE_BREAKPAD_GUARD_REGION
  // If we increased size to accommodate a Breakpad guard region, move
  // the effective address, if valid, upwards by the size of the guard region.
  if (addr_ == MAP_FAILED)
    return;
  if (addr_ < reinterpret_cast<void*>(kBreakpadGuardRegionBytes)) {
    LOG_ERROR("Map address %p is too low to accommodate Breakpad guard",
              addr_);
    effective_addr_ = MAP_FAILED;
  } else {
    effective_addr_ = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(addr_) + kBreakpadGuardRegionBytes);
  }
#endif
}

// Helper for LoadLibrary(). Return the actual size of the library loaded
// at |addr| in |load_size|, and the min vaddr in |min_vaddr|. Returns false
// if the library appears not to be loaded.
bool GetLibraryLoadSize(void* addr, size_t* load_size, size_t* min_vaddr) {
  LOG_INFO("Called for %p", addr);

  // Find the real load size and min vaddr for the library loaded at |addr|.
  CallbackData callback_data(addr);
  int status = 0;
  if (!DlIteratePhdr(&FindLoadedLibrarySize, &callback_data, &status)) {
    LOG_ERROR("No dl_iterate_phdr function found");
    return false;
  }
  if (!status) {
    LOG_ERROR("Failed to find library at address %p", addr);
    return false;
  }

  *load_size = callback_data.load_size;
  *min_vaddr = callback_data.min_vaddr;
  return true;
}

// Helper for LoadLibrary(). We reserve an address space larger than
// needed. After library loading we want to trim that reservation to only
// what is needed. Failure to trim should not occur, but if it does then
// everything will still run, so we treat it as a warning rather than
// an error.
void ResizeReservedAddressSpace(void* addr,
                                size_t reserved_size,
                                size_t load_size,
                                size_t min_vaddr) {
  LOG_INFO("Called for %p, reserved %d, loaded %d, min_vaddr %d",
           addr, static_cast<int>(reserved_size),
           static_cast<int>(load_size), static_cast<int>(min_vaddr));

  const uintptr_t uintptr_addr = reinterpret_cast<uintptr_t>(addr);

  if (reserved_size > load_size) {
    // Unmap the part of the reserved address space that is beyond the end of
    // the loaded library data.
    void* unmap = reinterpret_cast<void*>(uintptr_addr + load_size);
    const size_t length = reserved_size - load_size;
    if (munmap(unmap, length) == -1) {
      LOG_ERROR("WARNING: unmap of %d bytes at %p failed: %s",
                static_cast<int>(length), unmap, strerror(errno));
    }
  } else {
    LOG_ERROR("WARNING: library reservation was too small");
  }

#if RESERVE_BREAKPAD_GUARD_REGION
  if (kBreakpadGuardRegionBytes > min_vaddr) {
    // Unmap the part of the reserved address space that is ahead of where we
    // actually need the guard region to start. Resizes the guard region to
    // min_vaddr bytes.
    void* unmap =
        reinterpret_cast<void*>(uintptr_addr - kBreakpadGuardRegionBytes);
    const size_t length = kBreakpadGuardRegionBytes - min_vaddr;
    if (munmap(unmap, length) == -1) {
      LOG_ERROR("WARNING: unmap of %d bytes at %p failed: %s",
                static_cast<int>(length), unmap, strerror(errno));
    }
  } else {
    LOG_ERROR("WARNING: breakpad guard region reservation was too small");
  }
#endif
}

// Load a library with the chromium linker, using android_dlopen_ext().
//
// android_dlopen_ext() understands how to directly load from a zipfile,
// based on the format of |dlopen_ext_path|. If it contains a "!/" separator
// then the string indicates <zip_path>!/<file_path> and indicates the
// file_path element within the zip file at zip_path. A library in a
// zipfile must be uncompressed and page aligned. The library is expected
// to be lib/<abi_tag>/crazy.<basename>. The <abi_tag> used will be the
// same as the abi for this linker. The "crazy." prefix is included
// so that the Android Package Manager doesn't extract the library into
// /data/app-lib.
//
// If |dlopen_ext_path| contains no "!/" separator then android_dlopen_ext()
// assumes that it is a normal path to a standalone library file.
//
// Loading the library will also call its JNI_OnLoad() method, which
// shall register its methods. Note that lazy native method resolution
// will _not_ work after this, because Dalvik uses the system's dlsym()
// which won't see the new library, so explicit registration is mandatory.
//
// |env| is the current JNI environment handle.
// |clazz| is the static class handle for org.chromium.base.Linker,
// and is ignored here.
// |dlopen_ext_path| is the library identifier (e.g. libfoo.so).
// |load_address| is an explicit load address.
// |relro_path| is the path to the file into which RELRO data is held.
// |lib_info_obj| is a LibInfo handle used to communicate information
// with the Java side.
// Return true on success.
jboolean LoadLibrary(JNIEnv* env,
                     jclass clazz,
                     jstring dlopen_ext_path,
                     jlong load_address,
                     jobject lib_info_obj) {
  String dlopen_library_path(env, dlopen_ext_path);
  LOG_INFO("Called for %s, at address 0x%llx",
           dlopen_library_path.c_str(), load_address);

  if (!IsValidAddress(load_address)) {
    LOG_ERROR("Invalid address 0x%llx", load_address);
    return false;
  }

  const size_t size = kAddressSpaceReservationSize;
  void* wanted_addr = reinterpret_cast<void*>(load_address);

  // Reserve the address space into which we load the library.
  ScopedAnonymousMmap mapping(wanted_addr, size);
  void* addr = mapping.GetAddr();
  if (addr == MAP_FAILED) {
    LOG_ERROR("Failed to reserve space for load");
    return false;
  }
  if (wanted_addr && addr != wanted_addr) {
    LOG_ERROR("Failed to obtain fixed address for load");
    return false;
  }

  // Build dlextinfo to load the library into the reserved space, using
  // the shared RELRO if supplied and if its start address matches addr.
  int relro_fd = -1;
  int flags = ANDROID_DLEXT_RESERVED_ADDRESS;
  if (wanted_addr && lib_info_obj) {
    void* relro_start;
    s_lib_info_fields.GetRelroInfo(env, lib_info_obj,
                                   reinterpret_cast<size_t*>(&relro_start),
                                   nullptr, &relro_fd);
    if (relro_fd != -1 && relro_start == addr) {
      flags |= ANDROID_DLEXT_USE_RELRO;
    }
  }
  AndroidDlextinfo dlextinfo(flags, addr, size, relro_fd);

  // Load the library into the reserved space.
  const char* path = dlopen_library_path.c_str();
  void* handle = nullptr;
  if (!AndroidDlopenExt(path, RTLD_NOW, &dlextinfo, &handle)) {
    LOG_ERROR("No android_dlopen_ext function found");
    return false;
  }
  if (handle == nullptr) {
    LOG_ERROR("android_dlopen_ext: %s", dlerror());
    return false;
  }

  // For https://crbug.com/568880.
  //
  // Release the scoped mapping. Now that the library has loaded we can no
  // longer assume we have control of all of this area. libdl knows addr and
  // has loaded the library into some portion of the reservation. It will
  // not expect that portion of memory to be arbitrarily unmapped.
  mapping.Release();

  // After loading we can find the actual size of the library. It should
  // be less than the space we reserved for it.
  size_t load_size = 0;
  size_t min_vaddr = 0;
  if (!GetLibraryLoadSize(addr, &load_size, &min_vaddr)) {
    LOG_ERROR("Unable to find size for load at %p", addr);
    return false;
  }

  // Trim the reservation mapping to match the library's actual size. Failure
  // to resize is not a fatal error. At worst we lose a portion of virtual
  // address space that we might otherwise have recovered. Note that trimming
  // the mapping here requires that we have already released the scoped
  // mapping.
  ResizeReservedAddressSpace(addr, size, load_size, min_vaddr);

  // Locate and if found then call the loaded library's JNI_OnLoad() function.
  using JNI_OnLoadFunctionPtr = int (*)(void* vm, void* reserved);
  auto jni_onload =
      reinterpret_cast<JNI_OnLoadFunctionPtr>(dlsym(handle, "JNI_OnLoad"));
  if (jni_onload != nullptr) {
    // Check that JNI_OnLoad returns a usable JNI version.
    int jni_version = (*jni_onload)(s_java_vm, nullptr);
    if (jni_version < JNI_VERSION_1_4) {
      LOG_ERROR("JNI version is invalid: %d", jni_version);
      return false;
    }
  }

  // Note the load address and load size in the supplied libinfo object.
  const size_t cast_addr = reinterpret_cast<size_t>(addr);
  s_lib_info_fields.SetLoadInfo(env, lib_info_obj, cast_addr, load_size);

  LOG_INFO("Success loading library %s", dlopen_library_path.c_str());
  return true;
}

// Create a shared RELRO file for a library, using android_dlopen_ext().
//
// Loads the library similarly to LoadLibrary() above, by reserving address
// space and then using android_dlopen_ext() to load into the reserved
// area. Adds flags to android_dlopen_ext() to saved the library's RELRO
// memory into the given file path, then unload the library and returns.
//
// Does not call JNI_OnLoad() or otherwise execute any code from the library.
//
// |env| is the current JNI environment handle.
// |clazz| is the static class handle for org.chromium.base.Linker,
// and is ignored here.
// |dlopen_ext_path| is the library identifier (e.g. libfoo.so).
// |load_address| is an explicit load address.
// |relro_path| is the path to the file into which RELRO data is written.
// |lib_info_obj| is a LibInfo handle used to communicate information
// with the Java side.
// Return true on success.
jboolean CreateSharedRelro(JNIEnv* env,
                           jclass clazz,
                           jstring dlopen_ext_path,
                           jlong load_address,
                           jstring relro_path,
                           jobject lib_info_obj) {
  String dlopen_library_path(env, dlopen_ext_path);
  LOG_INFO("Called for %s, at address 0x%llx",
           dlopen_library_path.c_str(), load_address);

  if (!IsValidAddress(load_address) || load_address == 0) {
    LOG_ERROR("Invalid address 0x%llx", load_address);
    return false;
  }

  const size_t size = kAddressSpaceReservationSize;
  void* wanted_addr = reinterpret_cast<void*>(load_address);

  // Reserve the address space into which we load the library.
  ScopedAnonymousMmap mapping(wanted_addr, size);
  void* addr = mapping.GetAddr();
  if (addr == MAP_FAILED) {
    LOG_ERROR("Failed to reserve space for load");
    return false;
  }
  if (addr != wanted_addr) {
    LOG_ERROR("Failed to obtain fixed address for load");
    return false;
  }

  // Open the shared RELRO file for write. Overwrites any prior content.
  String shared_relro_path(env, relro_path);
  const char* filepath = shared_relro_path.c_str();
  unlink(filepath);
  int relro_fd = open(filepath, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (relro_fd == -1) {
    LOG_ERROR("open: %s: %s", filepath, strerror(errno));
    return false;
  }

  // Use android_dlopen_ext() to create the shared RELRO.
  const int flags = ANDROID_DLEXT_RESERVED_ADDRESS
                    | ANDROID_DLEXT_WRITE_RELRO;
  AndroidDlextinfo dlextinfo(flags, addr, size, relro_fd);

  const char* path = dlopen_library_path.c_str();
  void* handle = nullptr;
  if (!AndroidDlopenExt(path, RTLD_NOW, &dlextinfo, &handle)) {
    LOG_ERROR("No android_dlopen_ext function found");
    close(relro_fd);
    return false;
  }
  if (handle == nullptr) {
    LOG_ERROR("android_dlopen_ext: %s", dlerror());
    close(relro_fd);
    return false;
  }

  // For https://crbug.com/568880.
  //
  // Release the scoped mapping. See comment in LoadLibrary() above for more.
  mapping.Release();

  // For https://crbug.com/568880.
  //
  // Unload the library from this address. Calling dlclose() will unmap the
  // part of the reservation occupied by the libary, but will leave the
  // remainder of the reservation mapped, and we have no effective way of
  // unmapping the leftover portions because we don't know where dlclose's
  // unmap ended.
  //
  // For now we live with this. It is a loss of some virtual address space
  // (but not actual memory), and because it occurs only once and only in
  // the browser process, and never in renderer processes, it is not a
  // significant issue.
  //
  // TODO(simonb): Between mapping.Release() and here, consider calling the
  // functions that trim the reservation down to the size of the loaded
  // library. This may help recover some or all of the virtual address space
  // that is otherwise lost.
  dlclose(handle);

  // Reopen the shared RELRO fd in read-only mode. This ensures that nothing
  // can write to it through the RELRO fd that we return in libinfo.
  close(relro_fd);
  relro_fd = open(filepath, O_RDONLY);
  if (relro_fd == -1) {
    LOG_ERROR("open: %s: %s", filepath, strerror(errno));
    return false;
  }

  // Delete the directory entry for the RELRO file. The fd we hold ensures
  // that its data remains intact.
  if (unlink(filepath) == -1) {
    LOG_ERROR("unlink: %s: %s", filepath, strerror(errno));
    return false;
  }

  // Note the shared RELRO fd in the supplied libinfo object. In this
  // implementation the RELRO start is set to the library's load address,
  // and the RELRO size is unused.
  const size_t cast_addr = reinterpret_cast<size_t>(addr);
  s_lib_info_fields.SetRelroInfo(env, lib_info_obj, cast_addr, 0, relro_fd);

  LOG_INFO("Success creating shared RELRO %s", shared_relro_path.c_str());
  return true;
}

const JNINativeMethod kNativeMethods[] = {
    {"nativeGetCpuAbi",
     "("
     ")"
     "Ljava/lang/String;",
     reinterpret_cast<void*>(&GetCpuAbi)},
    {"nativeLoadLibrary",
     "("
     "Ljava/lang/String;"
     "J"
     "Lorg/chromium/base/library_loader/Linker$LibInfo;"
     ")"
     "Z",
     reinterpret_cast<void*>(&LoadLibrary)},
    {"nativeCreateSharedRelro",
     "("
     "Ljava/lang/String;"
     "J"
     "Ljava/lang/String;"
     "Lorg/chromium/base/library_loader/Linker$LibInfo;"
     ")"
     "Z",
     reinterpret_cast<void*>(&CreateSharedRelro)},
};

const size_t kNumNativeMethods =
    sizeof(kNativeMethods) / sizeof(kNativeMethods[0]);

}  // namespace

bool ModernLinkerJNIInit(JavaVM* vm, JNIEnv* env) {
  LOG_INFO("Entering");

  // Register native methods.
  jclass linker_class;
  if (!InitClassReference(env,
                          "org/chromium/base/library_loader/ModernLinker",
                          &linker_class))
    return false;

  LOG_INFO("Registering native methods");
  if (env->RegisterNatives(linker_class, kNativeMethods, kNumNativeMethods) < 0)
    return false;

  // Record the Java VM handle.
  s_java_vm = vm;

  return true;
}

}  // namespace chromium_android_linker
