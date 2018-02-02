// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CACHING_CERT_VERIFIER_H_
#define NET_CERT_CACHING_CERT_VERIFIER_H_

#include <memory>

#include "net/base/expiring_cache.h"
#include "net/base/net_export.h"
#include "net/cert/cert_database.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/cert_verify_result.h"

namespace net {

// CertVerifier that caches the results of certificate verifications.
//
// In general, certificate verification results will vary on only three
// parameters:
//   - The time of validation (as certificates are only valid for a period of
//     time)
//   - The revocation status (a certificate may be revoked at any time, but
//     revocation statuses themselves have validity period, so a 'good' result
//     may be reused for a period of time)
//   - The trust settings (a user may change trust settings at any time)
//
// This class tries to optimize by allowing certificate verification results
// to be cached for a limited amount of time (presently, 30 minutes), which
// tries to balance the implementation complexity of needing to monitor the
// above for meaningful changes and the practical utility of being able to
// cache results when they're not expected to change.
class NET_EXPORT CachingCertVerifier : public CertVerifier,
                                       public CertDatabase::Observer {
 public:
  // Visitor class to allow read-only inspection of the verification cache.
  class NET_EXPORT CacheVisitor {
   public:
    virtual ~CacheVisitor() {}

    // Called once for each entry in the cache, providing details about the
    // cached entry.
    // Returns true to continue iteration, or false to abort.
    virtual bool VisitEntry(const RequestParams& params,
                            int error,
                            const CertVerifyResult& verify_result,
                            base::Time verification_time,
                            base::Time expiration_time) = 0;
  };

  // Creates a CachingCertVerifier that will use |verifier| to perform the
  // actual verifications if they're not already cached or if the cached
  // item has expired.
  explicit CachingCertVerifier(std::unique_ptr<CertVerifier> verifier);

  ~CachingCertVerifier() override;

  // CertVerifier implementation:
  int Verify(const RequestParams& params,
             CRLSet* crl_set,
             CertVerifyResult* verify_result,
             const CompletionCallback& callback,
             std::unique_ptr<Request>* out_req,
             const NetLogWithSource& net_log) override;
  bool SupportsOCSPStapling() override;

  // Opportunistically attempts to add |error| and |verify_result| as the
  // result for |params|, which was obtained at |verification_time| and
  // expires at |expiration_time|.
  // This is opportunistic because it is not guaranteed that the entry
  // will be added (such as if the cache is full or an entry already
  // exists).
  // Returns true if the entry was added.
  bool AddEntry(const RequestParams& params,
                int error,
                const CertVerifyResult& verify_result,
                base::Time verification_time);

  // Iterates through all of the non-expired entries in the cache, calling
  // VisitEntry on |visitor| for each, until either all entries are
  // iterated through or the |visitor| aborts.
  // Note: During this call, it is not safe to call any non-const methods
  // on the CachingCertVerifier.
  void VisitEntries(CacheVisitor* visitor) const;

 private:
  FRIEND_TEST_ALL_PREFIXES(CachingCertVerifierTest, CacheHit);
  FRIEND_TEST_ALL_PREFIXES(CachingCertVerifierTest, Visitor);
  FRIEND_TEST_ALL_PREFIXES(CachingCertVerifierTest, AddsEntries);
  FRIEND_TEST_ALL_PREFIXES(CachingCertVerifierTest, DifferentCACerts);

  // CachedResult contains the result of a certificate verification.
  struct NET_EXPORT_PRIVATE CachedResult {
    CachedResult();
    ~CachedResult();

    int error;                // The return value of CertVerifier::Verify.
    CertVerifyResult result;  // The output of CertVerifier::Verify.
  };

  // Rather than having a single validity point along a monotonically increasing
  // timeline, certificate verification is based on falling within a range of
  // the certificate's NotBefore and NotAfter and based on what the current
  // system clock says (which may advance forwards or backwards as users correct
  // clock skew). CacheValidityPeriod and CacheExpirationFunctor are helpers to
  // ensure that expiration is measured both by the 'general' case (now + cache
  // TTL) and by whether or not significant enough clock skew was introduced
  // since the last verification.
  struct CacheValidityPeriod {
    explicit CacheValidityPeriod(base::Time now);
    CacheValidityPeriod(base::Time now, base::Time expiration);

    base::Time verification_time;
    base::Time expiration_time;
  };

  struct CacheExpirationFunctor {
    // Returns true iff |now| is within the validity period of |expiration|.
    bool operator()(const CacheValidityPeriod& now,
                    const CacheValidityPeriod& expiration) const;
  };

  using CertVerificationCache = ExpiringCache<RequestParams,
                                              CachedResult,
                                              CacheValidityPeriod,
                                              CacheExpirationFunctor>;

  // Handles completion of the request matching |params|, which started at
  // |start_time|, completing. |verify_result| and |result| are added to the
  // cache, and then |callback| (the original caller's callback) is invoked.
  void OnRequestFinished(const RequestParams& params,
                         base::Time start_time,
                         const CompletionCallback& callback,
                         CertVerifyResult* verify_result,
                         int error);

  // Adds |verify_result| and |error| to the cache for |params|, whose
  // verification attempt began at |start_time|. See the implementation
  // for more details about the necessity of |start_time|.
  void AddResultToCache(const RequestParams& params,
                        base::Time start_time,
                        const CertVerifyResult& verify_result,
                        int error);

  // CertDatabase::Observer methods:
  void OnCertDBChanged() override;

  // For unit testing.
  void ClearCache();
  size_t GetCacheSize() const;
  uint64_t cache_hits() const { return cache_hits_; }
  uint64_t requests() const { return requests_; }

  std::unique_ptr<CertVerifier> verifier_;

  CertVerificationCache cache_;

  uint64_t requests_;
  uint64_t cache_hits_;

  DISALLOW_COPY_AND_ASSIGN(CachingCertVerifier);
};

}  // namespace net

#endif  // NET_CERT_CACHING_CERT_VERIFIER_H_
