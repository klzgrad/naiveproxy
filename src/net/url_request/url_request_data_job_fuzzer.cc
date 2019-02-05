// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "base/memory/singleton.h"
#include "base/run_loop.h"
#include "base/test/fuzzed_data_provider.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/data_protocol_handler.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "net/url_request/url_request_test_util.h"

namespace {

const size_t kMaxLengthForFuzzedRange = 32;

}  // namespace

// This class tests creating and reading to completion a URLRequest with fuzzed
// input. The fuzzer provides a data: URL and optionally generates custom Range
// headers. The amount of data read in each Read call is also fuzzed, as is
// the size of the IOBuffer to read data into.
class URLRequestDataJobFuzzerHarness : public net::URLRequest::Delegate {
 public:
  URLRequestDataJobFuzzerHarness()
      : task_runner_(base::ThreadTaskRunnerHandle::Get()), context_(true) {
    job_factory_.SetProtocolHandler(
        "data", std::make_unique<net::DataProtocolHandler>());
    context_.set_job_factory(&job_factory_);
    context_.Init();
  }

  static URLRequestDataJobFuzzerHarness* GetInstance() {
    return base::Singleton<URLRequestDataJobFuzzerHarness>::get();
  }

  int CreateAndReadFromDataURLRequest(const uint8_t* data, size_t size) {
    base::FuzzedDataProvider provider(data, size);
    read_lengths_.clear();

    // Allocate an IOBuffer with fuzzed size.
    int buf_size = provider.ConsumeIntegralInRange(1, 127);  // 7 bits.
    buf_ = base::MakeRefCounted<net::IOBufferWithSize>(buf_size);

    // Generate a range header, and a bool determining whether to use it.
    // Generate the header regardless of the bool value to keep the data URL and
    // header in consistent byte addresses so the fuzzer doesn't have to work as
    // hard.
    bool use_range = provider.ConsumeBool();
    std::string range = provider.ConsumeBytesAsString(kMaxLengthForFuzzedRange);

    // Generate a sequence of reads sufficient to read the entire data URL,
    // capping it at 20000 reads, to avoid hangs. Once the limit is reached,
    // all subsequent reads will be 32k.
    size_t simulated_bytes_read = 0;
    while (simulated_bytes_read < provider.remaining_bytes() &&
           read_lengths_.size() < 20000u) {
      size_t read_length = provider.ConsumeIntegralInRange(1, buf_size);
      read_lengths_.push_back(read_length);
      simulated_bytes_read += read_length;
    }

    // The data URL is the rest of the fuzzed data with "data:" prepended, to
    // ensure that if it's a URL, it's a data URL. If the URL is invalid just
    // use a test variant, so the fuzzer has a chance to execute something.
    std::string data_url_string =
        std::string("data:") + provider.ConsumeRemainingBytesAsString();
    GURL data_url(data_url_string);
    if (!data_url.is_valid())
      data_url = GURL("data:text/html;charset=utf-8,<p>test</p>");

    // Create a URLRequest with the given data URL and start reading
    // from it.
    std::unique_ptr<net::URLRequest> request = context_.CreateRequest(
        data_url, net::DEFAULT_PRIORITY, this, TRAFFIC_ANNOTATION_FOR_TESTS);
    if (use_range) {
      if (!net::HttpUtil::IsValidHeaderValue(range))
        range = "bytes=3-";
      request->SetExtraRequestHeaderByName("Range", range, true);
    }

    // Block the thread while the request is read.
    base::RunLoop read_loop;
    read_loop_ = &read_loop;
    request->Start();
    read_loop.Run();
    read_loop_ = nullptr;
    return 0;
  }

  void QuitLoop() {
    DCHECK(read_loop_);
    task_runner_->PostTask(FROM_HERE, read_loop_->QuitClosure());
  }

  void ReadFromRequest(net::URLRequest* request) {
    int bytes_read = 0;
    do {
      size_t read_size = 32 * 1024;
      // If possible, pop the next read size.
      if (read_lengths_.size() > 0) {
        read_size = read_lengths_.back();
        read_lengths_.pop_back();
      }
      if (read_size > static_cast<size_t>(buf_->size()))
        buf_ = base::MakeRefCounted<net::IOBufferWithSize>(read_size);

      bytes_read = request->Read(buf_.get(), read_size);
    } while (bytes_read > 0);

    if (bytes_read != net::ERR_IO_PENDING)
      QuitLoop();
  }

  // net::URLRequest::Delegate:
  void OnReceivedRedirect(net::URLRequest* request,
                          const net::RedirectInfo& redirect_info,
                          bool* defer_redirect) override {}
  void OnAuthRequired(net::URLRequest* request,
                      net::AuthChallengeInfo* auth_info) override {}
  void OnCertificateRequested(
      net::URLRequest* request,
      net::SSLCertRequestInfo* cert_request_info) override {}
  void OnSSLCertificateError(net::URLRequest* request,
                             const net::SSLInfo& ssl_info,
                             bool fatal) override {}
  void OnResponseStarted(net::URLRequest* request, int net_error) override {
    DCHECK(buf_.get());
    DCHECK(read_loop_);
    DCHECK_NE(net::ERR_IO_PENDING, net_error);

    if (net_error == net::OK) {
      ReadFromRequest(request);
    } else {
      QuitLoop();
    }
  }
  void OnReadCompleted(net::URLRequest* request, int bytes_read) override {
    DCHECK_NE(net::ERR_IO_PENDING, bytes_read);
    DCHECK(buf_.get());
    DCHECK(read_loop_);

    if (bytes_read > 0) {
      ReadFromRequest(request);
    } else {
      QuitLoop();
    }
  }

 private:
  friend struct base::DefaultSingletonTraits<URLRequestDataJobFuzzerHarness>;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  net::TestURLRequestContext context_;
  net::URLRequestJobFactoryImpl job_factory_;
  std::vector<size_t> read_lengths_;
  scoped_refptr<net::IOBufferWithSize> buf_;
  base::RunLoop* read_loop_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(URLRequestDataJobFuzzerHarness);
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Using a static singleton test harness lets the test run ~3-4x faster.
  return URLRequestDataJobFuzzerHarness::GetInstance()
      ->CreateAndReadFromDataURLRequest(data, size);
}
