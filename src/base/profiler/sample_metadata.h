// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_SAMPLE_METADATA_H_
#define BASE_PROFILER_SAMPLE_METADATA_H_

#include "base/profiler/metadata_recorder.h"
#include "base/strings/string_piece.h"

// -----------------------------------------------------------------------------
// Usage documentation
// -----------------------------------------------------------------------------
//
// Overview:
// These functions provide a means to control the metadata attached to samples
// collected by the stack sampling profiler. Metadata state is shared between
// all threads within a process.
//
// Any samples collected by the sampling profiler will include the active
// metadata. This enables us to later analyze targeted subsets of samples
// (e.g. those collected during paint or layout).
//
// For example:
//
//   void DidStartLoad() {
//     base::SetSampleMetadata("Renderer.IsLoading", 1);
//   }
//
//   void DidFinishLoad() {
//     base::RemoveSampleMetadata("Renderer.IsLoading");
//   }
//
// Alternatively, ScopedSampleMetadata can be used to ensure that the metadata
// is removed correctly.
//
// For example:
//
//   void DoExpensiveWork() {
//     base::ScopedSampleMetadata metadata("xyz", 1);
//     if (...) {
//       ...
//       if (...) {
//         ...
//         return;
//       }
//     }
//     ...
//   }

namespace base {

class BASE_EXPORT ScopedSampleMetadata {
 public:
  ScopedSampleMetadata(base::StringPiece name, int64_t value);
  ScopedSampleMetadata(const ScopedSampleMetadata&) = delete;
  ~ScopedSampleMetadata();

  ScopedSampleMetadata& operator=(const ScopedSampleMetadata&) = delete;

 private:
  const uint64_t name_hash_;
};

// Sets a name hash/value pair in the process global stack sampling profiler
// metadata, overwriting any previous value set for that name hash.
BASE_EXPORT void SetSampleMetadata(base::StringPiece name, int64_t value);

// Removes the metadata item with the specified name hash from the process
// global stack sampling profiler metadata.
//
// If such an item doesn't exist, this has no effect.
BASE_EXPORT void RemoveSampleMetadata(base::StringPiece name);

// Returns the process-global metadata recorder instance used for tracking
// sampling profiler metadata.
//
// This function should not be called by non-profiler related code.
BASE_EXPORT base::MetadataRecorder* GetSampleMetadataRecorder();

}  // namespace base

#endif  // BASE_PROFILER_SAMPLE_METADATA_H_
