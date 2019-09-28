// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/trial_comparison_cert_verifier.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/post_task.h"
#include "base/values.h"
#include "build/build_config.h"
#include "crypto/sha2.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_verify_proc.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/ev_root_ca_metadata.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/multi_threaded_cert_verifier.h"
#include "net/cert/x509_util.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"

namespace net {

namespace {

base::Value TrialVerificationJobResultParams(bool trial_success) {
  base::Value results(base::Value::Type::DICTIONARY);
  results.SetBoolKey("trial_success", trial_success);
  return results;
}

bool CertVerifyResultEqual(const CertVerifyResult& a,
                           const CertVerifyResult& b) {
  return std::tie(a.cert_status, a.is_issued_by_known_root) ==
             std::tie(b.cert_status, b.is_issued_by_known_root) &&
         (!!a.verified_cert == !!b.verified_cert) &&
         (!a.verified_cert ||
          a.verified_cert->EqualsIncludingChain(b.verified_cert.get()));
}

scoped_refptr<ParsedCertificate> ParsedCertificateFromBuffer(
    CRYPTO_BUFFER* cert_handle,
    CertErrors* errors) {
  return ParsedCertificate::Create(bssl::UpRef(cert_handle),
                                   x509_util::DefaultParseCertificateOptions(),
                                   errors);
}

ParsedCertificateList ParsedCertificateListFromX509Certificate(
    const X509Certificate* cert) {
  CertErrors parsing_errors;

  ParsedCertificateList certs;
  scoped_refptr<ParsedCertificate> target =
      ParsedCertificateFromBuffer(cert->cert_buffer(), &parsing_errors);
  if (!target)
    return {};
  certs.push_back(target);

  for (const auto& buf : cert->intermediate_buffers()) {
    scoped_refptr<ParsedCertificate> intermediate =
        ParsedCertificateFromBuffer(buf.get(), &parsing_errors);
    if (!intermediate)
      return {};
    certs.push_back(intermediate);
  }

  return certs;
}

// Tests whether cert has multiple EV policies, and at least one matches the
// root. This is not a complete test of EV, but just enough to give a possible
// explanation as to why the platform verifier did not validate as EV while
// builtin did. (Since only the builtin verifier correctly handles multiple
// candidate EV policies.)
bool CertHasMultipleEVPoliciesAndOneMatchesRoot(const X509Certificate* cert) {
  if (cert->intermediate_buffers().empty())
    return false;

  ParsedCertificateList certs = ParsedCertificateListFromX509Certificate(cert);
  if (certs.empty())
    return false;

  ParsedCertificate* leaf = certs.front().get();
  ParsedCertificate* root = certs.back().get();

  if (!leaf->has_policy_oids())
    return false;

  const EVRootCAMetadata* ev_metadata = EVRootCAMetadata::GetInstance();
  std::set<der::Input> candidate_oids;
  for (const der::Input& oid : leaf->policy_oids()) {
    if (ev_metadata->IsEVPolicyOIDGivenBytes(oid))
      candidate_oids.insert(oid);
  }

  if (candidate_oids.size() <= 1)
    return false;

  SHA256HashValue root_fingerprint;
  crypto::SHA256HashString(root->der_cert().AsStringPiece(),
                           root_fingerprint.data,
                           sizeof(root_fingerprint.data));

  for (const der::Input& oid : candidate_oids) {
    if (ev_metadata->HasEVPolicyOIDGivenBytes(root_fingerprint, oid))
      return true;
  }

  return false;
}

}  // namespace

class TrialComparisonCertVerifier::TrialVerificationJob {
 public:
  TrialVerificationJob(const CertVerifier::Config& config,
                       const CertVerifier::RequestParams& params,
                       const NetLogWithSource& source_net_log,
                       TrialComparisonCertVerifier* cert_verifier,
                       int primary_error,
                       const CertVerifyResult& primary_result)
      : config_(config),
        config_changed_(false),
        params_(params),
        net_log_(
            NetLogWithSource::Make(source_net_log.net_log(),
                                   NetLogSourceType::TRIAL_CERT_VERIFIER_JOB)),
        cert_verifier_(cert_verifier),
        primary_error_(primary_error),
        primary_result_(primary_result) {
    net_log_.BeginEvent(NetLogEventType::TRIAL_CERT_VERIFIER_JOB);
    source_net_log.AddEventReferencingSource(
        NetLogEventType::TRIAL_CERT_VERIFIER_JOB_COMPARISON_STARTED,
        net_log_.source());
  }

  ~TrialVerificationJob() {
    if (cert_verifier_) {
      net_log_.AddEvent(NetLogEventType::CANCELLED);
      net_log_.EndEvent(NetLogEventType::TRIAL_CERT_VERIFIER_JOB);
    }
  }

  void Start() {
    // Unretained is safe because trial_request_ will cancel the callback on
    // destruction.
    int rv = cert_verifier_->trial_verifier()->Verify(
        params_, &trial_result_,
        base::BindOnce(&TrialVerificationJob::OnJobCompleted,
                       base::Unretained(this)),
        &trial_request_, net_log_);
    if (rv != ERR_IO_PENDING)
      OnJobCompleted(rv);
  }

  void OnConfigChanged() { config_changed_ = true; }

  void Finish(bool is_success, TrialComparisonResult result_code) {
    TrialComparisonCertVerifier* cert_verifier = cert_verifier_;
    cert_verifier_ = nullptr;

    UMA_HISTOGRAM_ENUMERATION("Net.CertVerifier_TrialComparisonResult",
                              result_code);

    net_log_.EndEvent(NetLogEventType::TRIAL_CERT_VERIFIER_JOB, [&] {
      return TrialVerificationJobResultParams(is_success);
    });

    if (!is_success) {
      cert_verifier->report_callback_.Run(
          params_.hostname(), params_.certificate(),
          config_.enable_rev_checking,
          config_.require_rev_checking_local_anchors,
          config_.enable_sha1_local_anchors,
          config_.disable_symantec_enforcement, primary_result_, trial_result_);
    }

    // |this| is deleted after RemoveJob returns.
    cert_verifier->RemoveJob(this);
  }

  void FinishSuccess(TrialComparisonResult result_code) {
    Finish(true /* is_success */, result_code);
  }

  void FinishWithError() {
    DCHECK(trial_error_ != primary_error_ ||
           !CertVerifyResultEqual(trial_result_, primary_result_));

    TrialComparisonResult result_code = kInvalid;

    if (primary_error_ == OK && trial_error_ == OK) {
      result_code = kBothValidDifferentDetails;
    } else if (primary_error_ == OK) {
      result_code = kPrimaryValidSecondaryError;
    } else if (trial_error_ == OK) {
      result_code = kPrimaryErrorSecondaryValid;
    } else {
      result_code = kBothErrorDifferentDetails;
    }
    Finish(false /* is_success */, result_code);
  }

  void OnJobCompleted(int trial_result_error) {
    DCHECK(primary_result_.verified_cert);
    DCHECK(trial_result_.verified_cert);

    trial_error_ = trial_result_error;

    bool errors_equal = trial_result_error == primary_error_;
    bool details_equal = CertVerifyResultEqual(trial_result_, primary_result_);
    bool trial_success = errors_equal && details_equal;

    if (trial_success) {
      FinishSuccess(kEqual);
      return;
    }

#if defined(OS_MACOSX)
    if (primary_error_ == ERR_CERT_REVOKED && !config_.enable_rev_checking &&
        !(primary_result_.cert_status & CERT_STATUS_REV_CHECKING_ENABLED) &&
        !(trial_result_.cert_status &
          (CERT_STATUS_REVOKED | CERT_STATUS_REV_CHECKING_ENABLED))) {
      if (config_changed_) {
        FinishSuccess(kIgnoredConfigurationChanged);
        return;
      }
      // CertVerifyProcMac does some revocation checking even if we didn't want
      // it. Try verifying with the trial verifier with revocation checking
      // enabled, see if it then returns REVOKED.

      int rv = cert_verifier_->revocation_trial_verifier()->Verify(
          params_, &reverification_result_,
          base::BindOnce(
              &TrialVerificationJob::OnMacRevcheckingReverificationJobCompleted,
              base::Unretained(this)),
          &reverification_request_, net_log_);
      if (rv != ERR_IO_PENDING)
        OnMacRevcheckingReverificationJobCompleted(rv);
      return;
    }
#endif

    const bool chains_equal =
        primary_result_.verified_cert->EqualsIncludingChain(
            trial_result_.verified_cert.get());

    if (!chains_equal && (trial_error_ == OK || primary_error_ != OK)) {
      if (config_changed_) {
        FinishSuccess(kIgnoredConfigurationChanged);
        return;
      }
      // Chains were different, reverify the trial_result_.verified_cert chain
      // using the platform verifier and compare results again.
      RequestParams reverification_params(
          trial_result_.verified_cert, params_.hostname(), params_.flags(),
          params_.ocsp_response(), params_.sct_list());

      int rv = cert_verifier_->primary_reverifier()->Verify(
          reverification_params, &reverification_result_,
          base::BindOnce(&TrialVerificationJob::
                             OnPrimaryReverifiyWithSecondaryChainCompleted,
                         base::Unretained(this)),
          &reverification_request_, net_log_);
      if (rv != ERR_IO_PENDING)
        OnPrimaryReverifiyWithSecondaryChainCompleted(rv);
      return;
    }

    TrialComparisonResult ignorable_difference =
        IsSynchronouslyIgnorableDifference(primary_error_, primary_result_,
                                           trial_error_, trial_result_);
    if (ignorable_difference != kInvalid) {
      FinishSuccess(ignorable_difference);
      return;
    }

    FinishWithError();
  }

  // Check if the differences between the primary and trial verifiers can be
  // ignored. This only handles differences that can be checked synchronously.
  // If the difference is ignorable, returns the relevant TrialComparisonResult,
  // otherwise returns kInvalid.
  static TrialComparisonResult IsSynchronouslyIgnorableDifference(
      int primary_error,
      const CertVerifyResult& primary_result,
      int trial_error,
      const CertVerifyResult& trial_result) {
    DCHECK(primary_result.verified_cert);
    DCHECK(trial_result.verified_cert);

    if (primary_error == OK &&
        primary_result.verified_cert->intermediate_buffers().empty()) {
      // Platform may support trusting a leaf certificate directly. Builtin
      // verifier does not. See https://crbug.com/814994.
      return kIgnoredLocallyTrustedLeaf;
    }

    const bool chains_equal =
        primary_result.verified_cert->EqualsIncludingChain(
            trial_result.verified_cert.get());

    if (chains_equal && (trial_result.cert_status & CERT_STATUS_IS_EV) &&
        !(primary_result.cert_status & CERT_STATUS_IS_EV) &&
        (primary_error == trial_error)) {
      // The platform CertVerifyProc impls only check a single potential EV
      // policy from the leaf.  If the leaf had multiple policies, builtin
      // verifier may verify it as EV when the platform verifier did not.
      if (CertHasMultipleEVPoliciesAndOneMatchesRoot(
              trial_result.verified_cert.get())) {
        return kIgnoredMultipleEVPoliciesAndOneMatchesRoot;
      }
    }
    return kInvalid;
  }

#if defined(OS_MACOSX)
  void OnMacRevcheckingReverificationJobCompleted(int reverification_error) {
    if (reverification_error == ERR_CERT_REVOKED) {
      FinishSuccess(kIgnoredMacUndesiredRevocationChecking);
      return;
    }
    FinishWithError();
  }
#endif

  void OnPrimaryReverifiyWithSecondaryChainCompleted(int reverification_error) {
    if (reverification_error == trial_error_ &&
        CertVerifyResultEqual(reverification_result_, trial_result_)) {
      // The new result matches the builtin verifier, so this was just
      // a difference in the platform's path-building ability.
      // Ignore the difference.
      FinishSuccess(kIgnoredDifferentPathReVerifiesEquivalent);
      return;
    }

    if (IsSynchronouslyIgnorableDifference(reverification_error,
                                           reverification_result_, trial_error_,
                                           trial_result_) != kInvalid) {
      // The new result matches if ignoring differences. Still use the
      // |kIgnoredDifferentPathReVerifiesEquivalent| code rather than the
      // result of IsSynchronouslyIgnorableDifference, since it's the higher
      // level description of what the difference is in this case.
      FinishSuccess(kIgnoredDifferentPathReVerifiesEquivalent);
      return;
    }

    FinishWithError();
  }

 private:
  const CertVerifier::Config config_;
  bool config_changed_;
  const CertVerifier::RequestParams params_;
  const NetLogWithSource net_log_;
  TrialComparisonCertVerifier* cert_verifier_;  // Non-owned.

  // Results from the trial verification.
  int trial_error_;
  CertVerifyResult trial_result_;
  std::unique_ptr<CertVerifier::Request> trial_request_;

  // Saved results of the primary verification.
  int primary_error_;
  const CertVerifyResult primary_result_;

  // Results from re-verification attempt.
  CertVerifyResult reverification_result_;
  std::unique_ptr<CertVerifier::Request> reverification_request_;

  DISALLOW_COPY_AND_ASSIGN(TrialVerificationJob);
};

TrialComparisonCertVerifier::TrialComparisonCertVerifier(
    bool initial_allowed,
    scoped_refptr<CertVerifyProc> primary_verify_proc,
    scoped_refptr<CertVerifyProc> trial_verify_proc,
    ReportCallback report_callback)
    : allowed_(initial_allowed),
      report_callback_(report_callback),
      primary_verifier_(
          MultiThreadedCertVerifier::CreateForDualVerificationTrial(
              primary_verify_proc,
              // Unretained is safe since the callback won't be called after
              // |primary_verifier_| is destroyed.
              base::BindRepeating(
                  &TrialComparisonCertVerifier::OnPrimaryVerifierComplete,
                  base::Unretained(this)),
              true /* should_record_histograms */)),
      primary_reverifier_(
          std::make_unique<MultiThreadedCertVerifier>(primary_verify_proc)),
      trial_verifier_(MultiThreadedCertVerifier::CreateForDualVerificationTrial(
          trial_verify_proc,
          // Unretained is safe since the callback won't be called after
          // |trial_verifier_| is destroyed.
          base::BindRepeating(
              &TrialComparisonCertVerifier::OnTrialVerifierComplete,
              base::Unretained(this)),
          false /* should_record_histograms */)),
      revocation_trial_verifier_(
          MultiThreadedCertVerifier::CreateForDualVerificationTrial(
              trial_verify_proc,
              // Unretained is safe since the callback won't be called after
              // |trial_verifier_| is destroyed.
              base::BindRepeating(
                  &TrialComparisonCertVerifier::OnTrialVerifierComplete,
                  base::Unretained(this)),
              false /* should_record_histograms */)) {
  CertVerifier::Config config;
  config.enable_rev_checking = true;
  revocation_trial_verifier_->SetConfig(config);
}

TrialComparisonCertVerifier::~TrialComparisonCertVerifier() = default;

int TrialComparisonCertVerifier::Verify(const RequestParams& params,
                                        CertVerifyResult* verify_result,
                                        CompletionOnceCallback callback,
                                        std::unique_ptr<Request>* out_req,
                                        const NetLogWithSource& net_log) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  return primary_verifier_->Verify(params, verify_result, std::move(callback),
                                   out_req, net_log);
}

void TrialComparisonCertVerifier::SetConfig(const Config& config) {
  config_ = config;

  primary_verifier_->SetConfig(config);
  primary_reverifier_->SetConfig(config);
  trial_verifier_->SetConfig(config);

  // Always enable revocation checking for the revocation trial verifier.
  CertVerifier::Config config_with_revocation = config;
  config_with_revocation.enable_rev_checking = true;
  revocation_trial_verifier_->SetConfig(config_with_revocation);

  // Notify all in-process jobs that the underlying configuration has changed.
  for (auto& job : jobs_) {
    job->OnConfigChanged();
  }
}

void TrialComparisonCertVerifier::OnPrimaryVerifierComplete(
    const RequestParams& params,
    const NetLogWithSource& net_log,
    int primary_error,
    const CertVerifyResult& primary_result,
    base::TimeDelta primary_latency,
    bool is_first_job) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!trial_allowed())
    return;

  // Only record the TrialPrimary histograms for the same set of requests
  // that TrialSecondary histograms will be recorded for, in order to get a
  // direct comparison.
  UMA_HISTOGRAM_CUSTOM_TIMES("Net.CertVerifier_Job_Latency_TrialPrimary",
                             primary_latency,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(10), 100);
  if (is_first_job) {
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "Net.CertVerifier_First_Job_Latency_TrialPrimary", primary_latency,
        base::TimeDelta::FromMilliseconds(1), base::TimeDelta::FromMinutes(10),
        100);
  }

  std::unique_ptr<TrialVerificationJob> job =
      std::make_unique<TrialVerificationJob>(config_, params, net_log, this,
                                             primary_error, primary_result);
  TrialVerificationJob* job_ptr = job.get();
  jobs_.insert(std::move(job));
  job_ptr->Start();
}

void TrialComparisonCertVerifier::OnTrialVerifierComplete(
    const RequestParams& params,
    const NetLogWithSource& net_log,
    int trial_error,
    const CertVerifyResult& trial_result,
    base::TimeDelta latency,
    bool is_first_job) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  UMA_HISTOGRAM_CUSTOM_TIMES("Net.CertVerifier_Job_Latency_TrialSecondary",
                             latency, base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(10), 100);
  if (is_first_job) {
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "Net.CertVerifier_First_Job_Latency_TrialSecondary", latency,
        base::TimeDelta::FromMilliseconds(1), base::TimeDelta::FromMinutes(10),
        100);
  }
}

void TrialComparisonCertVerifier::RemoveJob(TrialVerificationJob* job_ptr) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto it = jobs_.find(job_ptr);
  DCHECK(it != jobs_.end());
  jobs_.erase(it);
}

}  // namespace net
