// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/statistics_recorder.h"

#include <memory>

#include "base/at_exit.h"
#include "base/debug/leak_annotations.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_snapshot_manager.h"
#include "base/metrics/metrics_hashes.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/metrics/record_histogram_checker.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"

namespace {

// Initialize histogram statistics gathering system.
base::LazyInstance<base::StatisticsRecorder>::Leaky g_statistics_recorder_ =
    LAZY_INSTANCE_INITIALIZER;

bool HistogramNameLesser(const base::HistogramBase* a,
                         const base::HistogramBase* b) {
  return a->histogram_name() < b->histogram_name();
}

}  // namespace

namespace base {

StatisticsRecorder::~StatisticsRecorder() {
  DCHECK(histograms_);
  DCHECK(ranges_);

  // Clean out what this object created and then restore what existed before.
  Reset();
  base::AutoLock auto_lock(lock_.Get());
  histograms_ = existing_histograms_.release();
  callbacks_ = existing_callbacks_.release();
  ranges_ = existing_ranges_.release();
  providers_ = existing_providers_.release();
  record_checker_ = existing_record_checker_.release();
}

// static
void StatisticsRecorder::Initialize() {
  // Tests sometimes create local StatisticsRecorders in order to provide a
  // contained environment of histograms that can be later discarded. If a
  // true global instance gets created in this environment then it will
  // eventually get disconnected when the local instance destructs and
  // restores the previous state, resulting in no StatisticsRecorder at all.
  // The global lazy instance, however, will remain valid thus ensuring that
  // another never gets installed via this method. If a |histograms_| map
  // exists then assume the StatisticsRecorder is already "initialized".
  if (histograms_)
    return;

  // Ensure that an instance of the StatisticsRecorder object is created.
  g_statistics_recorder_.Get();
}

// static
bool StatisticsRecorder::IsActive() {
  base::AutoLock auto_lock(lock_.Get());
  return histograms_ != nullptr;
}

// static
void StatisticsRecorder::RegisterHistogramProvider(
    const WeakPtr<HistogramProvider>& provider) {
  providers_->push_back(provider);
}

// static
HistogramBase* StatisticsRecorder::RegisterOrDeleteDuplicate(
    HistogramBase* histogram) {
  HistogramBase* histogram_to_delete = nullptr;
  HistogramBase* histogram_to_return = nullptr;
  {
    base::AutoLock auto_lock(lock_.Get());
    if (!histograms_) {
      histogram_to_return = histogram;

      // As per crbug.com/79322 the histograms are intentionally leaked, so we
      // need to annotate them. Because ANNOTATE_LEAKING_OBJECT_PTR may be used
      // only once for an object, the duplicates should not be annotated.
      // Callers are responsible for not calling RegisterOrDeleteDuplicate(ptr)
      // twice |if (!histograms_)|.
      ANNOTATE_LEAKING_OBJECT_PTR(histogram);  // see crbug.com/79322
    } else {
      const std::string& name = histogram->histogram_name();
      HistogramMap::iterator it = histograms_->find(name);
      if (histograms_->end() == it) {
        // The StringKey references the name within |histogram| rather than
        // making a copy.
        (*histograms_)[name] = histogram;
        ANNOTATE_LEAKING_OBJECT_PTR(histogram);  // see crbug.com/79322
        // If there are callbacks for this histogram, we set the kCallbackExists
        // flag.
        auto callback_iterator = callbacks_->find(name);
        if (callback_iterator != callbacks_->end()) {
          if (!callback_iterator->second.is_null())
            histogram->SetFlags(HistogramBase::kCallbackExists);
          else
            histogram->ClearFlags(HistogramBase::kCallbackExists);
        }
        histogram_to_return = histogram;
      } else if (histogram == it->second) {
        // The histogram was registered before.
        histogram_to_return = histogram;
      } else {
        // We already have one histogram with this name.
        DCHECK_EQ(histogram->histogram_name(),
                  it->second->histogram_name()) << "hash collision";
        histogram_to_return = it->second;
        histogram_to_delete = histogram;
      }
    }
  }
  delete histogram_to_delete;
  return histogram_to_return;
}

// static
const BucketRanges* StatisticsRecorder::RegisterOrDeleteDuplicateRanges(
    const BucketRanges* ranges) {
  DCHECK(ranges->HasValidChecksum());
  std::unique_ptr<const BucketRanges> ranges_deleter;

  base::AutoLock auto_lock(lock_.Get());
  if (!ranges_) {
    ANNOTATE_LEAKING_OBJECT_PTR(ranges);
    return ranges;
  }

  std::list<const BucketRanges*>* checksum_matching_list;
  RangesMap::iterator ranges_it = ranges_->find(ranges->checksum());
  if (ranges_->end() == ranges_it) {
    // Add a new matching list to map.
    checksum_matching_list = new std::list<const BucketRanges*>();
    ANNOTATE_LEAKING_OBJECT_PTR(checksum_matching_list);
    (*ranges_)[ranges->checksum()] = checksum_matching_list;
  } else {
    checksum_matching_list = ranges_it->second;
  }

  for (const BucketRanges* existing_ranges : *checksum_matching_list) {
    if (existing_ranges->Equals(ranges)) {
      if (existing_ranges == ranges) {
        return ranges;
      } else {
        ranges_deleter.reset(ranges);
        return existing_ranges;
      }
    }
  }
  // We haven't found a BucketRanges which has the same ranges. Register the
  // new BucketRanges.
  checksum_matching_list->push_front(ranges);
  return ranges;
}

// static
void StatisticsRecorder::WriteHTMLGraph(const std::string& query,
                                        std::string* output) {
  if (!IsActive())
    return;

  Histograms snapshot;
  GetSnapshot(query, &snapshot);
  std::sort(snapshot.begin(), snapshot.end(), &HistogramNameLesser);
  for (const HistogramBase* histogram : snapshot) {
    histogram->WriteHTMLGraph(output);
    output->append("<br><hr><br>");
  }
}

// static
void StatisticsRecorder::WriteGraph(const std::string& query,
                                    std::string* output) {
  if (!IsActive())
    return;
  if (query.length())
    StringAppendF(output, "Collections of histograms for %s\n", query.c_str());
  else
    output->append("Collections of all histograms\n");

  Histograms snapshot;
  GetSnapshot(query, &snapshot);
  std::sort(snapshot.begin(), snapshot.end(), &HistogramNameLesser);
  for (const HistogramBase* histogram : snapshot) {
    histogram->WriteAscii(output);
    output->append("\n");
  }
}

// static
std::string StatisticsRecorder::ToJSON(const std::string& query) {
  if (!IsActive())
    return std::string();

  std::string output("{");
  if (!query.empty()) {
    output += "\"query\":";
    EscapeJSONString(query, true, &output);
    output += ",";
  }

  Histograms snapshot;
  GetSnapshot(query, &snapshot);
  output += "\"histograms\":[";
  bool first_histogram = true;
  for (const HistogramBase* histogram : snapshot) {
    if (first_histogram)
      first_histogram = false;
    else
      output += ",";
    std::string json;
    histogram->WriteJSON(&json);
    output += json;
  }
  output += "]}";
  return output;
}

// static
void StatisticsRecorder::GetHistograms(Histograms* output) {
  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_)
    return;

  for (const auto& entry : *histograms_) {
    output->push_back(entry.second);
  }
}

// static
void StatisticsRecorder::GetBucketRanges(
    std::vector<const BucketRanges*>* output) {
  base::AutoLock auto_lock(lock_.Get());
  if (!ranges_)
    return;

  for (const auto& entry : *ranges_) {
    for (auto* range_entry : *entry.second) {
      output->push_back(range_entry);
    }
  }
}

// static
HistogramBase* StatisticsRecorder::FindHistogram(base::StringPiece name) {
  // This must be called *before* the lock is acquired below because it will
  // call back into this object to register histograms. Those called methods
  // will acquire the lock at that time.
  ImportGlobalPersistentHistograms();

  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_)
    return nullptr;

  HistogramMap::iterator it = histograms_->find(name);
  if (histograms_->end() == it)
    return nullptr;
  return it->second;
}

// static
void StatisticsRecorder::ImportProvidedHistograms() {
  if (!providers_)
    return;

  // Merge histogram data from each provider in turn.
  for (const WeakPtr<HistogramProvider>& provider : *providers_) {
    // Weak-pointer may be invalid if the provider was destructed, though they
    // generally never are.
    if (provider)
      provider->MergeHistogramDeltas();
  }
}

// static
void StatisticsRecorder::PrepareDeltas(
    bool include_persistent,
    HistogramBase::Flags flags_to_set,
    HistogramBase::Flags required_flags,
    HistogramSnapshotManager* snapshot_manager) {
  if (include_persistent)
    ImportGlobalPersistentHistograms();

  auto known = GetKnownHistograms(include_persistent);
  snapshot_manager->PrepareDeltas(known.begin(), known.end(), flags_to_set,
                                  required_flags);
}

// static
void StatisticsRecorder::InitLogOnShutdown() {
  if (!histograms_)
    return;

  base::AutoLock auto_lock(lock_.Get());
  g_statistics_recorder_.Get().InitLogOnShutdownWithoutLock();
}

// static
void StatisticsRecorder::GetSnapshot(const std::string& query,
                                     Histograms* snapshot) {
  // This must be called *before* the lock is acquired below because it will
  // call back into this object to register histograms. Those called methods
  // will acquire the lock at that time.
  ImportGlobalPersistentHistograms();

  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_)
    return;

  for (const auto& entry : *histograms_) {
    if (entry.second->histogram_name().find(query) != std::string::npos)
      snapshot->push_back(entry.second);
  }
}

// static
bool StatisticsRecorder::SetCallback(
    const std::string& name,
    const StatisticsRecorder::OnSampleCallback& cb) {
  DCHECK(!cb.is_null());
  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_)
    return false;

  if (ContainsKey(*callbacks_, name))
    return false;
  callbacks_->insert(std::make_pair(name, cb));

  auto it = histograms_->find(name);
  if (it != histograms_->end())
    it->second->SetFlags(HistogramBase::kCallbackExists);

  return true;
}

// static
void StatisticsRecorder::ClearCallback(const std::string& name) {
  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_)
    return;

  callbacks_->erase(name);

  // We also clear the flag from the histogram (if it exists).
  auto it = histograms_->find(name);
  if (it != histograms_->end())
    it->second->ClearFlags(HistogramBase::kCallbackExists);
}

// static
StatisticsRecorder::OnSampleCallback StatisticsRecorder::FindCallback(
    const std::string& name) {
  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_)
    return OnSampleCallback();

  auto callback_iterator = callbacks_->find(name);
  return callback_iterator != callbacks_->end() ? callback_iterator->second
                                                : OnSampleCallback();
}

// static
size_t StatisticsRecorder::GetHistogramCount() {
  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_)
    return 0;
  return histograms_->size();
}

// static
void StatisticsRecorder::ForgetHistogramForTesting(base::StringPiece name) {
  if (!histograms_)
    return;

  HistogramMap::iterator found = histograms_->find(name);
  if (found == histograms_->end())
    return;

  HistogramBase* base = found->second;
  if (base->GetHistogramType() != SPARSE_HISTOGRAM) {
    // When forgetting a histogram, it's likely that other information is
    // also becoming invalid. Clear the persistent reference that may no
    // longer be valid. There's no danger in this as, at worst, duplicates
    // will be created in persistent memory.
    Histogram* histogram = static_cast<Histogram*>(base);
    histogram->bucket_ranges()->set_persistent_reference(0);
  }

  histograms_->erase(found);
}

// static
std::unique_ptr<StatisticsRecorder>
StatisticsRecorder::CreateTemporaryForTesting() {
  return WrapUnique(new StatisticsRecorder());
}

// static
void StatisticsRecorder::UninitializeForTesting() {
  // Stop now if it's never been initialized.
  if (!histograms_)
    return;

  // Get the global instance and destruct it. It's held in static memory so
  // can't "delete" it; call the destructor explicitly.
  DCHECK(g_statistics_recorder_.private_instance_);
  g_statistics_recorder_.Get().~StatisticsRecorder();

  // Now the ugly part. There's no official way to release a LazyInstance once
  // created so it's necessary to clear out an internal variable which
  // shouldn't be publicly visible but is for initialization reasons.
  g_statistics_recorder_.private_instance_ = 0;
}

// static
void StatisticsRecorder::SetRecordChecker(
    std::unique_ptr<RecordHistogramChecker> record_checker) {
  record_checker_ = record_checker.release();
}

// static
bool StatisticsRecorder::ShouldRecordHistogram(uint64_t histogram_hash) {
  return !record_checker_ || record_checker_->ShouldRecord(histogram_hash);
}

// static
std::vector<HistogramBase*> StatisticsRecorder::GetKnownHistograms(
    bool include_persistent) {
  std::vector<HistogramBase*> known;
  base::AutoLock auto_lock(lock_.Get());
  if (!histograms_ || histograms_->empty())
    return known;

  known.reserve(histograms_->size());
  for (const auto& h : *histograms_) {
    if (!include_persistent &&
        (h.second->flags() & HistogramBase::kIsPersistent)) {
      continue;
    }
    known.push_back(h.second);
  }

  return known;
}

// static
void StatisticsRecorder::ImportGlobalPersistentHistograms() {
  if (!histograms_)
    return;

  // Import histograms from known persistent storage. Histograms could have
  // been added by other processes and they must be fetched and recognized
  // locally. If the persistent memory segment is not shared between processes,
  // this call does nothing.
  GlobalHistogramAllocator* allocator = GlobalHistogramAllocator::Get();
  if (allocator)
    allocator->ImportHistogramsToStatisticsRecorder();
}

// This singleton instance should be started during the single threaded portion
// of main(), and hence it is not thread safe.  It initializes globals to
// provide support for all future calls.
StatisticsRecorder::StatisticsRecorder() {
  base::AutoLock auto_lock(lock_.Get());

  existing_histograms_.reset(histograms_);
  existing_callbacks_.reset(callbacks_);
  existing_ranges_.reset(ranges_);
  existing_providers_.reset(providers_);
  existing_record_checker_.reset(record_checker_);

  histograms_ = new HistogramMap;
  callbacks_ = new CallbackMap;
  ranges_ = new RangesMap;
  providers_ = new HistogramProviders;
  record_checker_ = nullptr;

  InitLogOnShutdownWithoutLock();
}

void StatisticsRecorder::InitLogOnShutdownWithoutLock() {
  if (!vlog_initialized_ && VLOG_IS_ON(1)) {
    vlog_initialized_ = true;
    AtExitManager::RegisterCallback(&DumpHistogramsToVlog, this);
  }
}

// static
void StatisticsRecorder::Reset() {
  std::unique_ptr<HistogramMap> histograms_deleter;
  std::unique_ptr<CallbackMap> callbacks_deleter;
  std::unique_ptr<RangesMap> ranges_deleter;
  std::unique_ptr<HistogramProviders> providers_deleter;
  std::unique_ptr<RecordHistogramChecker> record_checker_deleter;
  {
    base::AutoLock auto_lock(lock_.Get());
    histograms_deleter.reset(histograms_);
    callbacks_deleter.reset(callbacks_);
    ranges_deleter.reset(ranges_);
    providers_deleter.reset(providers_);
    record_checker_deleter.reset(record_checker_);
    histograms_ = nullptr;
    callbacks_ = nullptr;
    ranges_ = nullptr;
    providers_ = nullptr;
    record_checker_ = nullptr;
  }
  // We are going to leak the histograms and the ranges.
}

// static
void StatisticsRecorder::DumpHistogramsToVlog(void* instance) {
  std::string output;
  StatisticsRecorder::WriteGraph(std::string(), &output);
  VLOG(1) << output;
}


// static
StatisticsRecorder::HistogramMap* StatisticsRecorder::histograms_ = nullptr;
// static
StatisticsRecorder::CallbackMap* StatisticsRecorder::callbacks_ = nullptr;
// static
StatisticsRecorder::RangesMap* StatisticsRecorder::ranges_ = nullptr;
// static
StatisticsRecorder::HistogramProviders* StatisticsRecorder::providers_;
// static
RecordHistogramChecker* StatisticsRecorder::record_checker_ = nullptr;
// static
base::LazyInstance<base::Lock>::Leaky StatisticsRecorder::lock_ =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace base
