// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_TRIAL_COMPARISON_CERT_VERIFIER_H_
#define NET_CERT_TRIAL_COMPARISON_CERT_VERIFIER_H_

#include <stdint.h>

#include <memory>
#include <set>
#include <string>

#include "base/containers/unique_ptr_adapters.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "net/base/net_export.h"
#include "net/cert/cert_verifier.h"

namespace net {
class CertVerifyProc;

class NET_EXPORT TrialComparisonCertVerifier : public CertVerifier {
 public:
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum TrialComparisonResult {
    kInvalid = 0,
    kEqual = 1,
    kPrimaryValidSecondaryError = 2,
    kPrimaryErrorSecondaryValid = 3,
    kBothValidDifferentDetails = 4,
    kBothErrorDifferentDetails = 5,
    kIgnoredMacUndesiredRevocationChecking = 6,
    kIgnoredMultipleEVPoliciesAndOneMatchesRoot = 7,
    kIgnoredDifferentPathReVerifiesEquivalent = 8,
    kIgnoredLocallyTrustedLeaf = 9,
    kIgnoredConfigurationChanged = 10,
    kMaxValue = kIgnoredConfigurationChanged
  };

  using ReportCallback = base::RepeatingCallback<void(
      const std::string& hostname,
      const scoped_refptr<X509Certificate>& unverified_cert,
      bool enable_rev_checking,
      bool require_rev_checking_local_anchors,
      bool enable_sha1_local_anchors,
      bool disable_symantec_enforcement,
      const net::CertVerifyResult& primary_result,
      const net::CertVerifyResult& trial_result)>;

  TrialComparisonCertVerifier(bool initial_allowed,
                              scoped_refptr<CertVerifyProc> primary_verify_proc,
                              scoped_refptr<CertVerifyProc> trial_verify_proc,
                              ReportCallback report_callback);

  ~TrialComparisonCertVerifier() override;

  void set_trial_allowed(bool allowed) { allowed_ = allowed; }
  bool trial_allowed() const { return allowed_; }

  // CertVerifier implementation
  int Verify(const RequestParams& params,
             CertVerifyResult* verify_result,
             CompletionOnceCallback callback,
             std::unique_ptr<Request>* out_req,
             const NetLogWithSource& net_log) override;
  void SetConfig(const Config& config) override;

  // Returns a CertVerifier using the primary CertVerifyProc, which will not
  // cause OnPrimaryVerifierComplete to be called. This can be used to
  // attempt to re-verify a cert with different chain or flags without
  // messing up the stats or potentially causing an infinite loop.
  CertVerifier* primary_reverifier() const { return primary_reverifier_.get(); }
  CertVerifier* trial_verifier() const { return trial_verifier_.get(); }
  CertVerifier* revocation_trial_verifier() const {
    return revocation_trial_verifier_.get();
  }

 private:
  class TrialVerificationJob;

  void OnPrimaryVerifierComplete(const RequestParams& params,
                                 const NetLogWithSource& net_log,
                                 int primary_error,
                                 const CertVerifyResult& primary_result,
                                 base::TimeDelta primary_latency,
                                 bool is_first_job);
  void OnTrialVerifierComplete(const RequestParams& params,
                               const NetLogWithSource& net_log,
                               int trial_error,
                               const CertVerifyResult& trial_result,
                               base::TimeDelta latency,
                               bool is_first_job);

  void RemoveJob(TrialVerificationJob* job_ptr);

  // Whether the trial is allowed.
  bool allowed_;
  // Callback that reports are sent to.
  ReportCallback report_callback_;

  CertVerifier::Config config_;

  std::unique_ptr<CertVerifier> primary_verifier_;
  std::unique_ptr<CertVerifier> primary_reverifier_;
  std::unique_ptr<CertVerifier> trial_verifier_;
  // Similar to |trial_verifier_|, except configured to always check
  // revocation information.
  std::unique_ptr<CertVerifier> revocation_trial_verifier_;

  std::set<std::unique_ptr<TrialVerificationJob>, base::UniquePtrComparator>
      jobs_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(TrialComparisonCertVerifier);
};

}  // namespace net

#endif  // NET_CERT_TRIAL_COMPARISON_CERT_VERIFIER_H_
