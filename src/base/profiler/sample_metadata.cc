// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/sample_metadata.h"

#include "base/metrics/metrics_hashes.h"
#include "base/no_destructor.h"

namespace base {

ScopedSampleMetadata::ScopedSampleMetadata(base::StringPiece name,
                                           int64_t value)
    : name_hash_(HashMetricName(name)) {
  GetSampleMetadataRecorder()->Set(name_hash_, value);
}

ScopedSampleMetadata::~ScopedSampleMetadata() {
  GetSampleMetadataRecorder()->Remove(name_hash_);
}

void SetSampleMetadata(base::StringPiece name, int64_t value) {
  GetSampleMetadataRecorder()->Set(base::HashMetricName(name), value);
}

void RemoveSampleMetadata(base::StringPiece name) {
  GetSampleMetadataRecorder()->Remove(base::HashMetricName(name));
}

base::MetadataRecorder* GetSampleMetadataRecorder() {
  static base::NoDestructor<base::MetadataRecorder> instance;
  return instance.get();
}

}  // namespace base
