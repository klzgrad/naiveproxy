// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// StatisticsRecorder holds all Histograms and BucketRanges that are used by
// Histograms in the system. It provides a general place for
// Histograms/BucketRanges to register, and supports a global API for accessing
// (i.e., dumping, or graphing) the data.

#ifndef BASE_METRICS_STATISTICS_RECORDER_H_
#define BASE_METRICS_STATISTICS_RECORDER_H_

#include <stdint.h>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/record_histogram_checker.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/lock.h"

namespace base {

class BucketRanges;
class HistogramSnapshotManager;

class BASE_EXPORT StatisticsRecorder {
 public:
  // A class used as a key for the histogram map below. It always references
  // a string owned outside of this class, likely in the value of the map.
  class StringKey : public StringPiece {
   public:
    // Constructs the StringKey using various sources. The source must live
    // at least as long as the created object.
    StringKey(const std::string& str) : StringPiece(str) {}
    StringKey(StringPiece str) : StringPiece(str) {}

    // Though StringPiece is better passed by value than by reference, in
    // this case it's being passed many times and likely already been stored
    // in memory (not just registers) so the benefit of pass-by-value is
    // negated.
    bool operator<(const StringKey& rhs) const {
      // Since order is unimportant in the map and string comparisons can be
      // slow, use the length as the primary sort value.
      if (length() < rhs.length())
        return true;
      if (length() > rhs.length())
        return false;

      // Fall back to an actual string comparison. The lengths are the same
      // so a simple memory-compare is sufficient. This is slightly more
      // efficient than calling operator<() for StringPiece which would
      // again have to check lengths before calling wordmemcmp().
      return wordmemcmp(data(), rhs.data(), length()) < 0;
    }
  };

  // An interface class that allows the StatisticsRecorder to forcibly merge
  // histograms from providers when necessary.
  class HistogramProvider {
   public:
    // Merges all histogram information into the global versions.
    virtual void MergeHistogramDeltas() = 0;
  };

  typedef std::map<StringKey, HistogramBase*> HistogramMap;
  typedef std::vector<HistogramBase*> Histograms;
  typedef std::vector<WeakPtr<HistogramProvider>> HistogramProviders;

  ~StatisticsRecorder();

  // Initializes the StatisticsRecorder system. Safe to call multiple times.
  static void Initialize();

  // Find out if histograms can now be registered into our list.
  static bool IsActive();

  // Register a provider of histograms that can be called to merge those into
  // the global StatisticsRecorder. Calls to ImportProvidedHistograms() will
  // fetch from registered providers.
  static void RegisterHistogramProvider(
      const WeakPtr<HistogramProvider>& provider);

  // Register, or add a new histogram to the collection of statistics. If an
  // identically named histogram is already registered, then the argument
  // |histogram| will deleted.  The returned value is always the registered
  // histogram (either the argument, or the pre-existing registered histogram).
  static HistogramBase* RegisterOrDeleteDuplicate(HistogramBase* histogram);

  // Register, or add a new BucketRanges. If an identically BucketRanges is
  // already registered, then the argument |ranges| will deleted. The returned
  // value is always the registered BucketRanges (either the argument, or the
  // pre-existing one).
  static const BucketRanges* RegisterOrDeleteDuplicateRanges(
      const BucketRanges* ranges);

  // Methods for appending histogram data to a string.  Only histograms which
  // have |query| as a substring are written to |output| (an empty string will
  // process all registered histograms).
  static void WriteHTMLGraph(const std::string& query, std::string* output);
  static void WriteGraph(const std::string& query, std::string* output);

  // Returns the histograms with |verbosity_level| as the serialization
  // verbosity.
  static std::string ToJSON(JSONVerbosityLevel verbosity_level);

  // Method for extracting histograms which were marked for use by UMA.
  static void GetHistograms(Histograms* output);

  // Method for extracting BucketRanges used by all histograms registered.
  static void GetBucketRanges(std::vector<const BucketRanges*>* output);

  // Find a histogram by name. It matches the exact name. This method is thread
  // safe.  It returns NULL if a matching histogram is not found.
  static HistogramBase* FindHistogram(base::StringPiece name);

  // Imports histograms from providers. This must be called on the UI thread.
  static void ImportProvidedHistograms();

  // Snapshots all histograms via |snapshot_manager|. |flags_to_set| is used to
  // set flags for each histogram. |required_flags| is used to select
  // histograms to be recorded. Only histograms that have all the flags
  // specified by the argument will be chosen. If all histograms should be
  // recorded, set it to |Histogram::kNoFlags|.
  static void PrepareDeltas(bool include_persistent,
                            HistogramBase::Flags flags_to_set,
                            HistogramBase::Flags required_flags,
                            HistogramSnapshotManager* snapshot_manager);

  // GetSnapshot copies some of the pointers to registered histograms into the
  // caller supplied vector (Histograms). Only histograms which have |query| as
  // a substring are copied (an empty string will process all registered
  // histograms).
  static void GetSnapshot(const std::string& query, Histograms* snapshot);

  typedef base::Callback<void(HistogramBase::Sample)> OnSampleCallback;

  // SetCallback sets the callback to notify when a new sample is recorded on
  // the histogram referred to by |histogram_name|. The call to this method can
  // be be done before or after the histogram is created. This method is thread
  // safe. The return value is whether or not the callback was successfully set.
  static bool SetCallback(const std::string& histogram_name,
                          const OnSampleCallback& callback);

  // ClearCallback clears any callback set on the histogram referred to by
  // |histogram_name|. This method is thread safe.
  static void ClearCallback(const std::string& histogram_name);

  // FindCallback retrieves the callback for the histogram referred to by
  // |histogram_name|, or a null callback if no callback exists for this
  // histogram. This method is thread safe.
  static OnSampleCallback FindCallback(const std::string& histogram_name);

  // Returns the number of known histograms.
  static size_t GetHistogramCount();

  // Initializes logging histograms with --v=1. Safe to call multiple times.
  // Is called from ctor but for browser it seems that it is more useful to
  // start logging after statistics recorder, so we need to init log-on-shutdown
  // later.
  static void InitLogOnShutdown();

  // Removes a histogram from the internal set of known ones. This can be
  // necessary during testing persistent histograms where the underlying
  // memory is being released.
  static void ForgetHistogramForTesting(base::StringPiece name);

  // Creates a local StatisticsRecorder object for testing purposes. All new
  // histograms will be registered in it until it is destructed or pushed
  // aside for the lifetime of yet another SR object. The destruction of the
  // returned object will re-activate the previous one. Always release SR
  // objects in the opposite order to which they're created.
  static std::unique_ptr<StatisticsRecorder> CreateTemporaryForTesting()
      WARN_UNUSED_RESULT;

  // Resets any global instance of the statistics-recorder that was created
  // by a call to Initialize().
  static void UninitializeForTesting();

  // Sets the record checker for determining if a histogram should be recorded.
  // Record checker doesn't affect any already recorded histograms, so this
  // method must be called very early, before any threads have started.
  // Record checker methods can be called on any thread, so they shouldn't
  // mutate any state.
  // TODO(iburak): This is not yet hooked up to histogram recording
  // infrastructure.
  static void SetRecordChecker(
      std::unique_ptr<RecordHistogramChecker> record_checker);

  // Returns true iff the given histogram should be recorded based on
  // the ShouldRecord() method of the record checker.
  // If the record checker is not set, returns true.
  static bool ShouldRecordHistogram(uint64_t histogram_hash);

 private:
  // We keep a map of callbacks to histograms, so that as histograms are
  // created, we can set the callback properly.
  typedef std::map<std::string, OnSampleCallback> CallbackMap;

  // We keep all |bucket_ranges_| in a map, from checksum to a list of
  // |bucket_ranges_|.  Checksum is calculated from the |ranges_| in
  // |bucket_ranges_|.
  typedef std::map<uint32_t, std::list<const BucketRanges*>*> RangesMap;

  friend struct LazyInstanceTraitsBase<StatisticsRecorder>;
  friend class StatisticsRecorderTest;
  FRIEND_TEST_ALL_PREFIXES(StatisticsRecorderTest, IterationTest);

  // Fetch set of existing histograms. Ownership of the individual histograms
  // remains with the StatisticsRecorder.
  static std::vector<HistogramBase*> GetKnownHistograms(
      bool include_persistent);

  // Imports histograms from global persistent memory. The global lock must
  // not be held during this call.
  static void ImportGlobalPersistentHistograms();

  // The constructor just initializes static members. Usually client code should
  // use Initialize to do this. But in test code, you can friend this class and
  // call the constructor to get a clean StatisticsRecorder.
  StatisticsRecorder();

  // Initialize implementation but without lock. Caller should guard
  // StatisticsRecorder by itself if needed (it isn't in unit tests).
  void InitLogOnShutdownWithoutLock();

  // These are copies of everything that existed when the (test) Statistics-
  // Recorder was created. The global ones have to be moved aside to create a
  // clean environment.
  std::unique_ptr<HistogramMap> existing_histograms_;
  std::unique_ptr<CallbackMap> existing_callbacks_;
  std::unique_ptr<RangesMap> existing_ranges_;
  std::unique_ptr<HistogramProviders> existing_providers_;
  std::unique_ptr<RecordHistogramChecker> existing_record_checker_;

  bool vlog_initialized_ = false;

  static void Reset();
  static void DumpHistogramsToVlog(void* instance);

  static HistogramMap* histograms_;
  static CallbackMap* callbacks_;
  static RangesMap* ranges_;
  static HistogramProviders* providers_;
  static RecordHistogramChecker* record_checker_;

  // Lock protects access to above maps. This is a LazyInstance to avoid races
  // when the above methods are used before Initialize(). Previously each method
  // would do |if (!lock_) return;| which would race with
  // |lock_ = new Lock;| in StatisticsRecorder(). http://crbug.com/672852.
  static base::LazyInstance<base::Lock>::Leaky lock_;

  DISALLOW_COPY_AND_ASSIGN(StatisticsRecorder);
};

}  // namespace base

#endif  // BASE_METRICS_STATISTICS_RECORDER_H_
