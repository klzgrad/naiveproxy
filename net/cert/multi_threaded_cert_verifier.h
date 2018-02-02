// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_MULTI_THREADED_CERT_VERIFIER_H_
#define NET_CERT_MULTI_THREADED_CERT_VERIFIER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/cert/cert_verifier.h"

namespace net {

class CertVerifierJob;
class CertVerifierRequest;
class CertVerifyProc;

// MultiThreadedCertVerifier is a CertVerifier implementation that runs
// synchronous CertVerifier implementations on worker threads.
class NET_EXPORT_PRIVATE MultiThreadedCertVerifier : public CertVerifier {
 public:
  explicit MultiThreadedCertVerifier(scoped_refptr<CertVerifyProc> verify_proc);

  // When the verifier is destroyed, all certificate verifications requests are
  // canceled, and their completion callbacks will not be called.
  ~MultiThreadedCertVerifier() override;

  // CertVerifier implementation
  int Verify(const RequestParams& params,
             CRLSet* crl_set,
             CertVerifyResult* verify_result,
             const CompletionCallback& callback,
             std::unique_ptr<Request>* out_req,
             const NetLogWithSource& net_log) override;

  bool SupportsOCSPStapling() override;

 private:
  struct JobToRequestParamsComparator;
  friend class CertVerifierRequest;
  friend class CertVerifierJob;
  friend class MultiThreadedCertVerifierTest;
  FRIEND_TEST_ALL_PREFIXES(MultiThreadedCertVerifierTest, InflightJoin);
  FRIEND_TEST_ALL_PREFIXES(MultiThreadedCertVerifierTest, MultipleInflightJoin);
  FRIEND_TEST_ALL_PREFIXES(MultiThreadedCertVerifierTest, CancelRequest);

  struct JobComparator {
    bool operator()(const CertVerifierJob* job1,
                    const CertVerifierJob* job2) const;
  };

  // Returns an inflight job for |key|. If there is no such job then returns
  // null.
  CertVerifierJob* FindJob(const RequestParams& key);

  // Removes |job| from the inflight set, and passes ownership back to the
  // caller. |job| must already be |inflight_|.
  std::unique_ptr<CertVerifierJob> RemoveJob(CertVerifierJob* job);

  // For unit testing.
  uint64_t requests() const { return requests_; }
  uint64_t inflight_joins() const { return inflight_joins_; }

  // inflight_ holds the jobs for which an active verification is taking place,
  // mapping the job's raw pointer to an owned pointer. Would be a
  // set<unique_ptr> but extraction of owned objects from a set of owned types
  // doesn't come until C++17.
  std::map<CertVerifierJob*, std::unique_ptr<CertVerifierJob>, JobComparator>
      inflight_;

  uint64_t requests_;
  uint64_t inflight_joins_;

  scoped_refptr<CertVerifyProc> verify_proc_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(MultiThreadedCertVerifier);
};

}  // namespace net

#endif  // NET_CERT_MULTI_THREADED_CERT_VERIFIER_H_
