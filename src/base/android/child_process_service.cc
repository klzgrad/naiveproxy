// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/library_loader/library_loader_hooks.h"
#include "base/android/pre_freeze_background_memory_trimmer.h"
#include "base/debug/dump_without_crashing.h"
#include "base/file_descriptor_store.h"
#include "base/logging.h"
#include "base/posix/global_descriptors.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "base/process_launcher_jni/ChildProcessService_jni.h"

using base::android::JavaIntArrayToIntVector;
using base::android::JavaParamRef;

namespace base {
namespace android {

void JNI_ChildProcessService_RegisterFileDescriptors(
    JNIEnv* env,
    const JavaParamRef<jobjectArray>& j_keys,
    const JavaParamRef<jintArray>& j_ids,
    const JavaParamRef<jintArray>& j_fds,
    const JavaParamRef<jlongArray>& j_offsets,
    const JavaParamRef<jlongArray>& j_sizes) {
  std::vector<std::optional<std::string>> keys;
  JavaObjectArrayReader<jstring> keys_array(j_keys);
  keys.reserve(checked_cast<size_t>(keys_array.size()));
  for (auto str : keys_array) {
    std::optional<std::string> key;
    if (str) {
      key = base::android::ConvertJavaStringToUTF8(env, str);
    }
    keys.push_back(std::move(key));
  }

  std::vector<int> ids;
  base::android::JavaIntArrayToIntVector(env, j_ids, &ids);
  std::vector<int> fds;
  base::android::JavaIntArrayToIntVector(env, j_fds, &fds);
  std::vector<int64_t> offsets;
  base::android::JavaLongArrayToInt64Vector(env, j_offsets, &offsets);
  std::vector<int64_t> sizes;
  base::android::JavaLongArrayToInt64Vector(env, j_sizes, &sizes);

  DCHECK_EQ(keys.size(), ids.size());
  DCHECK_EQ(ids.size(), fds.size());
  DCHECK_EQ(fds.size(), offsets.size());
  DCHECK_EQ(offsets.size(), sizes.size());

  for (size_t i = 0; i < ids.size(); i++) {
    base::MemoryMappedFile::Region region = {offsets.at(i),
                                             static_cast<size_t>(sizes.at(i))};
    const std::optional<std::string>& key = keys.at(i);
    const auto id = static_cast<GlobalDescriptors::Key>(ids.at(i));
    int fd = fds.at(i);
    if (key) {
      base::FileDescriptorStore::GetInstance().Set(*key, base::ScopedFD(fd),
                                                   region);
    } else {
      base::GlobalDescriptors::GetInstance()->Set(id, fd, region);
    }
  }
}

void JNI_ChildProcessService_ExitChildProcess(JNIEnv* env) {
  VLOG(0) << "ChildProcessService: Exiting child process.";
  base::android::LibraryLoaderExitHook();
  _exit(0);
}

// Make sure this isn't inlined so it shows up in stack traces.
// the function body unique by adding a log line, so it doesn't get merged
// with other functions by link time optimizations (ICF).
NOINLINE void JNI_ChildProcessService_DumpProcessStack(JNIEnv* env) {
  LOG(ERROR) << "Dumping as requested.";
  base::debug::DumpWithoutCrashing();
}

void JNI_ChildProcessService_OnSelfFreeze(JNIEnv* env) {
  PreFreezeBackgroundMemoryTrimmer::OnSelfFreeze();
}

}  // namespace android
}  // namespace base
