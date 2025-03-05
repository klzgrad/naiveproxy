// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/histogram_base.h"

#include <limits.h>

#include <memory>
#include <set>
#include <string_view>
#include <utility>

#include "base/check_op.h"
#include "base/json/json_string_value_serializer.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/sparse_histogram.h"
#include "base/metrics/statistics_recorder.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/numerics/safe_conversions.h"
#include "base/pickle.h"
#include "base/process/process_handle.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/values.h"

namespace base {

std::string HistogramTypeToString(HistogramType type) {
  switch (type) {
    case HISTOGRAM:
      return "HISTOGRAM";
    case LINEAR_HISTOGRAM:
      return "LINEAR_HISTOGRAM";
    case BOOLEAN_HISTOGRAM:
      return "BOOLEAN_HISTOGRAM";
    case CUSTOM_HISTOGRAM:
      return "CUSTOM_HISTOGRAM";
    case SPARSE_HISTOGRAM:
      return "SPARSE_HISTOGRAM";
    case DUMMY_HISTOGRAM:
      return "DUMMY_HISTOGRAM";
  }
  NOTREACHED();
}

HistogramBase* DeserializeHistogramInfo(PickleIterator* iter) {
  int type;
  if (!iter->ReadInt(&type)) {
    return nullptr;
  }

  switch (type) {
    case HISTOGRAM:
      return Histogram::DeserializeInfoImpl(iter);
    case LINEAR_HISTOGRAM:
      return LinearHistogram::DeserializeInfoImpl(iter);
    case BOOLEAN_HISTOGRAM:
      return BooleanHistogram::DeserializeInfoImpl(iter);
    case CUSTOM_HISTOGRAM:
      return CustomHistogram::DeserializeInfoImpl(iter);
    case SPARSE_HISTOGRAM:
      return SparseHistogram::DeserializeInfoImpl(iter);
    default:
      return nullptr;
  }
}

HistogramBase::CountAndBucketData::CountAndBucketData(Count32 count,
                                                      int64_t sum,
                                                      Value::List buckets)
    : count(count), sum(sum), buckets(std::move(buckets)) {}

HistogramBase::CountAndBucketData::~CountAndBucketData() = default;

HistogramBase::CountAndBucketData::CountAndBucketData(
    CountAndBucketData&& other) = default;

HistogramBase::CountAndBucketData& HistogramBase::CountAndBucketData::operator=(
    CountAndBucketData&& other) = default;

const HistogramBase::Sample32 HistogramBase::kSampleType_MAX = INT_MAX;

HistogramBase::HistogramBase(const char* name)
    : histogram_name_(name), flags_(kNoFlags) {}

HistogramBase::~HistogramBase() = default;

void HistogramBase::CheckName(std::string_view name) const {
  DCHECK_EQ(std::string_view(histogram_name()), name)
      << "Provided histogram name doesn't match instance name. Are you using a "
         "dynamic string in a macro?";
}

void HistogramBase::SetFlags(int32_t flags) {
  flags_.fetch_or(flags, std::memory_order_relaxed);
}

void HistogramBase::ClearFlags(int32_t flags) {
  flags_.fetch_and(~flags, std::memory_order_relaxed);
}

bool HistogramBase::HasFlags(int32_t flags) const {
  // Check this->flags() is a superset of |flags|, i.e. every flag in |flags| is
  // included.
  return (this->flags() & flags) == flags;
}

void HistogramBase::AddScaled(Sample32 value, int count, int scale) {
  DCHECK_GT(scale, 0);

  // Convert raw count and probabilistically round up/down if the remainder
  // is more than a random number [0, scale). This gives a more accurate
  // count when there are a large number of records. RandInt is "inclusive",
  // hence the -1 for the max value.
  int count_scaled = count / scale;
  if (count - (count_scaled * scale) > base::RandInt(0, scale - 1)) {
    ++count_scaled;
  }
  if (count_scaled <= 0) {
    return;
  }

  AddCount(value, count_scaled);
}

void HistogramBase::AddKilo(Sample32 value, int count) {
  AddScaled(value, count, 1000);
}

void HistogramBase::AddKiB(Sample32 value, int count) {
  AddScaled(value, count, 1024);
}

void HistogramBase::AddTimeMillisecondsGranularity(const TimeDelta& time) {
  Add(saturated_cast<Sample32>(time.InMilliseconds()));
}

void HistogramBase::AddTimeMicrosecondsGranularity(const TimeDelta& time) {
  // Intentionally drop high-resolution reports on clients with low-resolution
  // clocks. High-resolution metrics cannot make use of low-resolution data and
  // reporting it merely adds noise to the metric. https://crbug.com/807615#c16
  if (TimeTicks::IsHighResolution()) {
    Add(saturated_cast<Sample32>(time.InMicroseconds()));
  }
}

void HistogramBase::AddBoolean(bool value) {
  Add(value ? 1 : 0);
}

void HistogramBase::SerializeInfo(Pickle* pickle) const {
  pickle->WriteInt(GetHistogramType());
  SerializeInfoImpl(pickle);
}

uint32_t HistogramBase::FindCorruption(const HistogramSamples& samples) const {
  // Not supported by default.
  return NO_INCONSISTENCIES;
}

void HistogramBase::WriteJSON(std::string* output,
                              JSONVerbosityLevel verbosity_level) const {
  CountAndBucketData count_and_bucket_data = GetCountAndBucketData();
  Value::Dict parameters = GetParameters();

  JSONStringValueSerializer serializer(output);
  Value::Dict root;
  root.Set("name", histogram_name());
  root.Set("count", count_and_bucket_data.count);
  root.Set("sum", static_cast<double>(count_and_bucket_data.sum));
  root.Set("flags", flags());
  root.Set("params", std::move(parameters));
  if (verbosity_level != JSON_VERBOSITY_LEVEL_OMIT_BUCKETS) {
    root.Set("buckets", std::move(count_and_bucket_data.buckets));
  }
  root.Set("pid", static_cast<int>(GetUniqueIdForProcess().GetUnsafeValue()));
  serializer.Serialize(root);
}

void HistogramBase::FindAndRunCallbacks(HistogramBase::Sample32 sample) const {
  StatisticsRecorder::GlobalSampleCallback global_sample_callback =
      StatisticsRecorder::global_sample_callback();
  if (global_sample_callback) {
    global_sample_callback(histogram_name(), name_hash(), sample);
  }

  // We check the flag first since it is very cheap and we can avoid the
  // function call and lock overhead of FindAndRunHistogramCallbacks().
  if (!HasFlags(kCallbackExists)) {
    return;
  }

  StatisticsRecorder::FindAndRunHistogramCallbacks(
      base::PassKey<HistogramBase>(), histogram_name(), name_hash(), sample);
}

HistogramBase::CountAndBucketData HistogramBase::GetCountAndBucketData() const {
  std::unique_ptr<HistogramSamples> snapshot = SnapshotSamples();
  Count32 count = snapshot->TotalCount();
  int64_t sum = snapshot->sum();
  std::unique_ptr<SampleCountIterator> it = snapshot->Iterator();

  Value::List buckets;
  while (!it->Done()) {
    Sample32 bucket_min;
    int64_t bucket_max;
    Count32 bucket_count;
    it->Get(&bucket_min, &bucket_max, &bucket_count);

    Value::Dict bucket_value;
    bucket_value.Set("low", bucket_min);
    // TODO(crbug.com/40228085): Make base::Value able to hold int64_t and
    // remove this cast.
    bucket_value.Set("high", static_cast<int>(bucket_max));
    bucket_value.Set("count", bucket_count);
    buckets.Append(std::move(bucket_value));
    it->Next();
  }

  return CountAndBucketData(count, sum, std::move(buckets));
}

void HistogramBase::WriteAsciiBucketGraph(double x_count,
                                          int line_length,
                                          std::string* output) const {
  int x_remainder = line_length - x_count;

  while (0 < x_count--) {
    output->append("-");
  }
  output->append("O");
  while (0 < x_remainder--) {
    output->append(" ");
  }
}

const std::string HistogramBase::GetSimpleAsciiBucketRange(
    Sample32 sample) const {
  return StringPrintf("%d", sample);
}

void HistogramBase::WriteAsciiBucketValue(Count32 current,
                                          double scaled_sum,
                                          std::string* output) const {
  StringAppendF(output, " (%d = %3.1f%%)", current, current / scaled_sum);
}

void HistogramBase::WriteAscii(std::string* output) const {
  base::Value::Dict graph_dict = ToGraphDict();
  output->append(*graph_dict.FindString("header"));
  output->append("\n");
  output->append(*graph_dict.FindString("body"));
}

// static
char const* HistogramBase::GetPermanentName(std::string_view name) {
  // A set of histogram names that provides the "permanent" lifetime required
  // by histogram objects for those strings that are not already code constants
  // or held in persistent memory.
  static base::NoDestructor<std::set<std::string, std::less<>>> permanent_names;
  static base::NoDestructor<Lock> permanent_names_lock;

  AutoLock lock(*permanent_names_lock);
  auto it = permanent_names->lower_bound(name);
  if (it == permanent_names->end() || *it != name) {
    it = permanent_names->emplace_hint(it, name);
  }
  return it->c_str();
}

}  // namespace base
