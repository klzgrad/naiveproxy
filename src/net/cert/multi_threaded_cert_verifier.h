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
#include "net/base/completion_once_callback.h"
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
  using VerifyCompleteCallback =
      base::RepeatingCallback<void(const RequestParams&,
                                   const NetLogWithSource&,
                                   int,
                                   const CertVerifyResult&,
                                   base::TimeDelta,
                                   bool)>;

  explicit MultiThreadedCertVerifier(scoped_refptr<CertVerifyProc> verify_proc);

  // When the verifier is destroyed, all certificate verifications requests are
  // canceled, and their completion callbacks will not be called.
  ~MultiThreadedCertVerifier() override;

  // Creates a MultiThreadedCertVerifier that will call
  // |verify_complete_callback| once for each verification that has completed
  // (if it is non-null). Histograms will have |histogram_suffix| appended, if
  // it is non-empty.
  // This factory method is temporary, and should not be used without
  // consulting with OWNERS.
  // TODO(mattm): remove this once the dual verification trial is complete.
  // (See https://crbug.com/649026.)
  static std::unique_ptr<MultiThreadedCertVerifier>
  CreateForDualVerificationTrial(
      scoped_refptr<CertVerifyProc> verify_proc,
      VerifyCompleteCallback verify_complete_callback,
      bool should_record_histograms);

  // CertVerifier implementation
  int Verify(const RequestParams& params,
             CertVerifyResult* verify_result,
             CompletionOnceCallback callback,
             std::unique_ptr<Request>* out_req,
             const NetLogWithSource& net_log) override;
  void SetConfig(const CertVerifier::Config& config) override;

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

  // TODO(mattm): remove this once the dual verification trial is complete.
  // (See https://crbug.com/649026.)
  MultiThreadedCertVerifier(scoped_refptr<CertVerifyProc> verify_proc,
                            VerifyCompleteCallback verify_complete_callback,
                            bool should_record_histograms);

  // Returns an inflight job for |key|, if it can be joined. If there is no
  // such job then returns null.
  CertVerifierJob* FindJob(const RequestParams& key);

  // Removes |job| from the inflight set, and passes ownership back to the
  // caller. |job| must already be |inflight_|.
  std::unique_ptr<CertVerifierJob> RemoveJob(CertVerifierJob* job);

  // For unit testing.
  uint64_t requests() const { return requests_; }
  uint64_t inflight_joins() const { return inflight_joins_; }

  // |joinable_| holds the jobs for which an active verification is taking
  // place and can be joined by new requests (e.g. the config is the same),
  // mapping the job's raw pointer to an owned pointer.
  // TODO(rsleevi): Once C++17 is supported, switch this to be a std::set<>,
  // which supports extracting owned objects from the set.
  std::map<CertVerifierJob*, std::unique_ptr<CertVerifierJob>, JobComparator>
      joinable_;

  // |inflight_| contains all jobs that are still undergoing active
  // verification, but which can no longer be joined - such as due to the
  // underlying configuration changing.
  std::map<CertVerifierJob*, std::unique_ptr<CertVerifierJob>, JobComparator>
      inflight_;

  uint32_t config_id_;
  Config config_;

  uint64_t requests_;
  uint64_t inflight_joins_;

  scoped_refptr<CertVerifyProc> verify_proc_;

  // Members for dual verification trial. TODO(mattm): Remove these.
  // (See https://crbug.com/649026.)
  VerifyCompleteCallback verify_complete_callback_;
  bool should_record_histograms_ = true;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(MultiThreadedCertVerifier);
};

}  // namespace net

#endif  // NET_CERT_MULTI_THREADED_CERT_VERIFIER_H_
