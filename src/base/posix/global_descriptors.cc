// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/posix/global_descriptors.h"

#include <utility>
#include <vector>

#include "base/logging.h"

namespace base {

GlobalDescriptors::Descriptor::Descriptor(Key key, int fd)
    : key(key), fd(fd), region(base::MemoryMappedFile::Region::kWholeFile) {}

GlobalDescriptors::Descriptor::Descriptor(Key key,
                                          int fd,
                                          base::MemoryMappedFile::Region region)
    : key(key), fd(fd), region(region) {}

// static
GlobalDescriptors* GlobalDescriptors::GetInstance() {
  typedef Singleton<base::GlobalDescriptors,
                    LeakySingletonTraits<base::GlobalDescriptors>>
      GlobalDescriptorsSingleton;
  return GlobalDescriptorsSingleton::get();
}

int GlobalDescriptors::Get(Key key) const {
  const int ret = MaybeGet(key);

  if (ret == -1) {
    DLOG(FATAL) << "Unknown global descriptor: " << key;
  }
  return ret;
}

int GlobalDescriptors::MaybeGet(Key key) const {
  for (const auto& i : descriptors_) {
    if (i.key == key) {
      return i.fd;
    }
  }

  return -1;
}

base::ScopedFD GlobalDescriptors::TakeFD(
    Key key,
    base::MemoryMappedFile::Region* region) {
  base::ScopedFD fd;
  for (auto i = descriptors_.begin(); i != descriptors_.end(); ++i) {
    if (i->key == key) {
      *region = i->region;
      fd.reset(i->fd);
      descriptors_.erase(i);
      break;
    }
  }
  return fd;
}

void GlobalDescriptors::Set(Key key, int fd) {
  Set(key, fd, base::MemoryMappedFile::Region::kWholeFile);
}

void GlobalDescriptors::Set(Key key,
                            int fd,
                            base::MemoryMappedFile::Region region) {
  for (auto& i : descriptors_) {
    if (i.key == key) {
      i.fd = fd;
      i.region = region;
      return;
    }
  }

  descriptors_.emplace_back(key, fd, region);
}

base::MemoryMappedFile::Region GlobalDescriptors::GetRegion(Key key) const {
  for (const auto& i : descriptors_) {
    if (i.key == key) {
      return i.region;
    }
  }
  DLOG(FATAL) << "Unknown global descriptor: " << key;
  return base::MemoryMappedFile::Region::kWholeFile;
}

void GlobalDescriptors::Reset(const Mapping& mapping) {
  descriptors_ = mapping;
}

GlobalDescriptors::GlobalDescriptors() = default;

GlobalDescriptors::~GlobalDescriptors() = default;

}  // namespace base
