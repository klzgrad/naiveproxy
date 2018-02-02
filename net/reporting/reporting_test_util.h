// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_TEST_UTIL_H_
#define NET_REPORTING_REPORTING_TEST_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_delegate.h"
#include "net/reporting/reporting_uploader.h"
#include "testing/gtest/include/gtest/gtest.h"

class GURL;

namespace base {
class MockTimer;
class SimpleTestClock;
class SimpleTestTickClock;
class Value;
}  // namespace base

namespace url {
class Origin;
}  // namespace url

namespace net {

class ReportingCache;
struct ReportingClient;
class ReportingGarbageCollector;

// Finds a particular client (by origin and endpoint) in the cache and returns
// it (or nullptr if not found).
const ReportingClient* FindClientInCache(const ReportingCache* cache,
                                         const url::Origin& origin,
                                         const GURL& endpoint);

// A test implementation of ReportingUploader that holds uploads for tests to
// examine and complete with a specified outcome.
class TestReportingUploader : public ReportingUploader {
 public:
  class PendingUpload {
   public:
    virtual ~PendingUpload();

    virtual const GURL& url() const = 0;
    virtual const std::string& json() const = 0;
    virtual std::unique_ptr<base::Value> GetValue() const = 0;

    virtual void Complete(Outcome outcome) = 0;

   protected:
    PendingUpload();
  };

  TestReportingUploader();
  ~TestReportingUploader() override;

  const std::vector<std::unique_ptr<PendingUpload>>& pending_uploads() const {
    return pending_uploads_;
  }

  // ReportingUploader implementation:
  void StartUpload(const GURL& url,
                   const std::string& json,
                   const Callback& callback) override;

 private:
  std::vector<std::unique_ptr<PendingUpload>> pending_uploads_;

  DISALLOW_COPY_AND_ASSIGN(TestReportingUploader);
};

class TestReportingDelegate : public ReportingDelegate {
 public:
  TestReportingDelegate();

  // ReportingDelegate implementation:

  ~TestReportingDelegate() override;

  bool CanQueueReport(const url::Origin& origin) const override;

  bool CanSendReport(const url::Origin& origin) const override;

  bool CanSetClient(const url::Origin& origin,
                    const GURL& endpoint) const override;

  bool CanUseClient(const url::Origin& origin,
                    const GURL& endpoint) const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(TestReportingDelegate);
};

// A test implementation of ReportingContext that uses test versions of
// Clock, TickClock, Timer, and ReportingUploader.
class TestReportingContext : public ReportingContext {
 public:
  TestReportingContext(const ReportingPolicy& policy);
  ~TestReportingContext();

  base::SimpleTestClock* test_clock() {
    return reinterpret_cast<base::SimpleTestClock*>(clock());
  }
  base::SimpleTestTickClock* test_tick_clock() {
    return reinterpret_cast<base::SimpleTestTickClock*>(tick_clock());
  }
  base::MockTimer* test_delivery_timer() { return delivery_timer_; }
  base::MockTimer* test_garbage_collection_timer() {
    return garbage_collection_timer_;
  }
  TestReportingUploader* test_uploader() {
    return reinterpret_cast<TestReportingUploader*>(uploader());
  }
  TestReportingDelegate* test_delegate() {
    return reinterpret_cast<TestReportingDelegate*>(delegate());
  }

 private:
  // Owned by the Persister and GarbageCollector, respectively, but referenced
  // here to preserve type:

  base::MockTimer* delivery_timer_;
  base::MockTimer* garbage_collection_timer_;

  DISALLOW_COPY_AND_ASSIGN(TestReportingContext);
};

// A unit test base class that provides a TestReportingContext and shorthand
// getters.
class ReportingTestBase : public ::testing::Test {
 protected:
  ReportingTestBase();
  ~ReportingTestBase() override;

  void UsePolicy(const ReportingPolicy& policy);

  // Simulates an embedder restart, preserving the ReportingPolicy.
  //
  // Advances the Clock by |delta|, and the TickClock by |delta_ticks|. Both can
  // be zero or negative.
  void SimulateRestart(base::TimeDelta delta, base::TimeDelta delta_ticks);

  TestReportingContext* context() { return context_.get(); }

  const ReportingPolicy& policy() { return context_->policy(); }

  base::SimpleTestClock* clock() { return context_->test_clock(); }
  base::SimpleTestTickClock* tick_clock() {
    return context_->test_tick_clock();
  }
  base::MockTimer* delivery_timer() { return context_->test_delivery_timer(); }
  base::MockTimer* garbage_collection_timer() {
    return context_->test_garbage_collection_timer();
  }
  TestReportingUploader* uploader() { return context_->test_uploader(); }

  ReportingCache* cache() { return context_->cache(); }
  ReportingEndpointManager* endpoint_manager() {
    return context_->endpoint_manager();
  }
  ReportingDeliveryAgent* delivery_agent() {
    return context_->delivery_agent();
  }
  ReportingGarbageCollector* garbage_collector() {
    return context_->garbage_collector();
  }

  ReportingPersister* persister() { return context_->persister(); }

  base::TimeTicks yesterday();
  base::TimeTicks now();
  base::TimeTicks tomorrow();

  const std::vector<std::unique_ptr<TestReportingUploader::PendingUpload>>&
  pending_uploads() {
    return uploader()->pending_uploads();
  }

 private:
  void CreateContext(const ReportingPolicy& policy,
                     base::Time now,
                     base::TimeTicks now_ticks);

  std::unique_ptr<TestReportingContext> context_;

  DISALLOW_COPY_AND_ASSIGN(ReportingTestBase);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_TEST_UTIL_H_
