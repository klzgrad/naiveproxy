// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/histogram_base.h"

#include <limits.h>

#include <memory>
#include <utility>

#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/sparse_histogram.h"
#include "base/metrics/statistics_recorder.h"
#include "base/pickle.h"
#include "base/process/process_handle.h"
#include "base/strings/stringprintf.h"
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
  }
  NOTREACHED();
  return "UNKNOWN";
}

HistogramBase* DeserializeHistogramInfo(PickleIterator* iter) {
  int type;
  if (!iter->ReadInt(&type))
    return NULL;

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
      return NULL;
  }
}

const HistogramBase::Sample HistogramBase::kSampleType_MAX = INT_MAX;
HistogramBase* HistogramBase::report_histogram_ = nullptr;

HistogramBase::HistogramBase(const std::string& name)
    : histogram_name_(name),
      flags_(kNoFlags) {}

HistogramBase::~HistogramBase() {}

void HistogramBase::CheckName(const StringPiece& name) const {
  DCHECK_EQ(histogram_name(), name);
}

void HistogramBase::SetFlags(int32_t flags) {
  HistogramBase::Count old_flags = subtle::NoBarrier_Load(&flags_);
  subtle::NoBarrier_Store(&flags_, old_flags | flags);
}

void HistogramBase::ClearFlags(int32_t flags) {
  HistogramBase::Count old_flags = subtle::NoBarrier_Load(&flags_);
  subtle::NoBarrier_Store(&flags_, old_flags & ~flags);
}

void HistogramBase::AddTime(const TimeDelta& time) {
  Add(static_cast<Sample>(time.InMilliseconds()));
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

bool HistogramBase::ValidateHistogramContents(bool crash_if_invalid,
                                              int corrupted_count) const {
  return true;
}

void HistogramBase::WriteJSON(std::string* output) const {
  Count count;
  int64_t sum;
  std::unique_ptr<ListValue> buckets(new ListValue());
  GetCountAndBucketData(&count, &sum, buckets.get());
  std::unique_ptr<DictionaryValue> parameters(new DictionaryValue());
  GetParameters(parameters.get());

  JSONStringValueSerializer serializer(output);
  DictionaryValue root;
  root.SetString("name", histogram_name());
  root.SetInteger("count", count);
  root.SetDouble("sum", static_cast<double>(sum));
  root.SetInteger("flags", flags());
  root.Set("params", std::move(parameters));
  root.Set("buckets", std::move(buckets));
  root.SetInteger("pid", GetUniqueIdForProcess());
  serializer.Serialize(root);
}

// static
void HistogramBase::EnableActivityReportHistogram(
    const std::string& process_type) {
  if (report_histogram_)
    return;

  size_t existing = StatisticsRecorder::GetHistogramCount();
  if (existing != 0) {
    DVLOG(1) << existing
             << " histograms were created before reporting was enabled.";
  }

  std::string name =
      "UMA.Histograms.Activity" +
      (process_type.empty() ? process_type : "." + process_type);

  // Calling FactoryGet() here rather than using a histogram-macro works
  // around some problems with tests that could end up seeing the results
  // histogram when not expected due to a bad interaction between
  // HistogramTester and StatisticsRecorder.
  report_histogram_ = LinearHistogram::FactoryGet(
      name, 1, HISTOGRAM_REPORT_MAX, HISTOGRAM_REPORT_MAX + 1,
      kUmaTargetedHistogramFlag);
  report_histogram_->Add(HISTOGRAM_REPORT_CREATED);
}

void HistogramBase::FindAndRunCallback(HistogramBase::Sample sample) const {
  if ((flags() & kCallbackExists) == 0)
    return;

  StatisticsRecorder::OnSampleCallback cb =
      StatisticsRecorder::FindCallback(histogram_name());
  if (!cb.is_null())
    cb.Run(sample);
}

void HistogramBase::WriteAsciiBucketGraph(double current_size,
                                          double max_size,
                                          std::string* output) const {
  const int k_line_length = 72;  // Maximal horizontal width of graph.
  int x_count = static_cast<int>(k_line_length * (current_size / max_size)
                                 + 0.5);
  int x_remainder = k_line_length - x_count;

  while (0 < x_count--)
    output->append("-");
  output->append("O");
  while (0 < x_remainder--)
    output->append(" ");
}

const std::string HistogramBase::GetSimpleAsciiBucketRange(
    Sample sample) const {
  return StringPrintf("%d", sample);
}

void HistogramBase::WriteAsciiBucketValue(Count current,
                                          double scaled_sum,
                                          std::string* output) const {
  StringAppendF(output, " (%d = %3.1f%%)", current, current/scaled_sum);
}

// static
void HistogramBase::ReportHistogramActivity(const HistogramBase& histogram,
                                            ReportActivity activity) {
  if (!report_histogram_)
    return;

  const int32_t flags = histogram.flags_;
  HistogramReport report_type = HISTOGRAM_REPORT_MAX;
  switch (activity) {
    case HISTOGRAM_CREATED:
      report_histogram_->Add(HISTOGRAM_REPORT_HISTOGRAM_CREATED);
      switch (histogram.GetHistogramType()) {
        case HISTOGRAM:
          report_type = HISTOGRAM_REPORT_TYPE_LOGARITHMIC;
          break;
        case LINEAR_HISTOGRAM:
          report_type = HISTOGRAM_REPORT_TYPE_LINEAR;
          break;
        case BOOLEAN_HISTOGRAM:
          report_type = HISTOGRAM_REPORT_TYPE_BOOLEAN;
          break;
        case CUSTOM_HISTOGRAM:
          report_type = HISTOGRAM_REPORT_TYPE_CUSTOM;
          break;
        case SPARSE_HISTOGRAM:
          report_type = HISTOGRAM_REPORT_TYPE_SPARSE;
          break;
      }
      report_histogram_->Add(report_type);
      if (flags & kIsPersistent)
        report_histogram_->Add(HISTOGRAM_REPORT_FLAG_PERSISTENT);
      if ((flags & kUmaStabilityHistogramFlag) == kUmaStabilityHistogramFlag)
        report_histogram_->Add(HISTOGRAM_REPORT_FLAG_UMA_STABILITY);
      else if (flags & kUmaTargetedHistogramFlag)
        report_histogram_->Add(HISTOGRAM_REPORT_FLAG_UMA_TARGETED);
      break;

    case HISTOGRAM_LOOKUP:
      report_histogram_->Add(HISTOGRAM_REPORT_HISTOGRAM_LOOKUP);
      break;
  }
}

}  // namespace base
