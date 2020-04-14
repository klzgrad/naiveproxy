// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/gtest_xml_unittest_result_printer.h"

#include "base/base64.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/test/test_switches.h"
#include "base/time/time.h"

namespace base {

namespace {
const int kDefaultTestPartResultsLimit = 10;

const char kTestPartLesultsLimitExceeded[] =
    "Test part results limit exceeded. Use --test-launcher-test-part-limit to "
    "increase or disable limit.";
}  // namespace

XmlUnitTestResultPrinter::XmlUnitTestResultPrinter()
    : output_file_(nullptr), open_failed_(false) {}

XmlUnitTestResultPrinter::~XmlUnitTestResultPrinter() {
  if (output_file_ && !open_failed_) {
    fprintf(output_file_, "</testsuites>\n");
    fflush(output_file_);
    CloseFile(output_file_);
  }
}

bool XmlUnitTestResultPrinter::Initialize(const FilePath& output_file_path) {
  DCHECK(!output_file_);
  output_file_ = OpenFile(output_file_path, "w");
  if (!output_file_) {
    // If the file open fails, we set the output location to stderr. This is
    // because in current usage our caller CHECKs the result of this function.
    // But that in turn causes a LogMessage that comes back to this object,
    // which in turn causes a (double) crash. By pointing at stderr, there might
    // be some indication what's going wrong. See https://crbug.com/736783.
    output_file_ = stderr;
    open_failed_ = true;
    return false;
  }

  fprintf(output_file_,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites>\n");
  fflush(output_file_);

  return true;
}

void XmlUnitTestResultPrinter::OnAssert(const char* file,
                                        int line,
                                        const std::string& summary,
                                        const std::string& message) {
  WriteTestPartResult(file, line, testing::TestPartResult::kFatalFailure,
                      summary, message);
}

void XmlUnitTestResultPrinter::OnTestCaseStart(
    const testing::TestCase& test_case) {
  fprintf(output_file_, "  <testsuite>\n");
  fflush(output_file_);
}

void XmlUnitTestResultPrinter::OnTestStart(
    const testing::TestInfo& test_info) {
  // This is our custom extension - it helps to recognize which test was
  // running when the test binary crashed. Note that we cannot even open the
  // <testcase> tag here - it requires e.g. run time of the test to be known.
  fprintf(output_file_,
          "    <x-teststart name=\"%s\" classname=\"%s\" />\n",
          test_info.name(),
          test_info.test_case_name());
  fflush(output_file_);
}

void XmlUnitTestResultPrinter::OnTestEnd(const testing::TestInfo& test_info) {
  fprintf(output_file_,
          "    <testcase name=\"%s\" status=\"run\" time=\"%.3f\""
          " classname=\"%s\">\n",
          test_info.name(),
          static_cast<double>(test_info.result()->elapsed_time()) /
              Time::kMillisecondsPerSecond,
          test_info.test_case_name());
  if (test_info.result()->Failed()) {
    fprintf(output_file_,
            "      <failure message=\"\" type=\"\"></failure>\n");
  }

  int limit = test_info.result()->total_part_count();
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kTestLauncherTestPartResultsLimit)) {
    std::string limit_str =
        CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            switches::kTestLauncherTestPartResultsLimit);
    int test_part_results_limit = std::strtol(limit_str.c_str(), nullptr, 10);
    if (test_part_results_limit >= 0)
      limit = std::min(limit, test_part_results_limit);
  } else {
    limit = std::min(limit, kDefaultTestPartResultsLimit);
  }

  for (int i = 0; i < limit; ++i) {
    const auto& test_part_result = test_info.result()->GetTestPartResult(i);
    WriteTestPartResult(test_part_result.file_name(),
                        test_part_result.line_number(), test_part_result.type(),
                        test_part_result.summary(), test_part_result.message());
  }

  if (test_info.result()->total_part_count() > limit) {
    WriteTestPartResult(
        "unknown", 0, testing::TestPartResult::kNonFatalFailure,
        kTestPartLesultsLimitExceeded, kTestPartLesultsLimitExceeded);
  }

  fprintf(output_file_, "    </testcase>\n");
  fflush(output_file_);
}

void XmlUnitTestResultPrinter::OnTestCaseEnd(
    const testing::TestCase& test_case) {
  fprintf(output_file_, "  </testsuite>\n");
  fflush(output_file_);
}

void XmlUnitTestResultPrinter::WriteTestPartResult(
    const char* file,
    int line,
    testing::TestPartResult::Type result_type,
    const std::string& summary,
    const std::string& message) {
  const char* type = "unknown";
  switch (result_type) {
    case testing::TestPartResult::kSuccess:
      type = "success";
      break;
    case testing::TestPartResult::kNonFatalFailure:
      type = "failure";
      break;
    case testing::TestPartResult::kFatalFailure:
      type = "fatal_failure";
      break;
    case testing::TestPartResult::kSkip:
      type = "skip";
      break;
  }
  std::string summary_encoded;
  Base64Encode(summary, &summary_encoded);
  std::string message_encoded;
  Base64Encode(message, &message_encoded);
  fprintf(output_file_,
          "      <x-test-result-part type=\"%s\" file=\"%s\" line=\"%d\">\n"
          "        <summary>%s</summary>\n"
          "        <message>%s</message>\n"
          "      </x-test-result-part>\n",
          type, file, line, summary_encoded.c_str(), message_encoded.c_str());
  fflush(output_file_);
}

}  // namespace base
