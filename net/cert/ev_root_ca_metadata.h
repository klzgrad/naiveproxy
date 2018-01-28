// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_EV_ROOT_CA_METADATA_H_
#define NET_CERT_EV_ROOT_CA_METADATA_H_

#include "build/build_config.h"

#if defined(USE_NSS_CERTS)
#include <secoidt.h>
#endif

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/cert/x509_certificate.h"

namespace base {
template <typename T>
struct LazyInstanceTraitsBase;
}  // namespace base

namespace net {

namespace der {
class Input;
}  // namespace der

// A singleton.  This class stores the meta data of the root CAs that issue
// extended-validation (EV) certificates.
class NET_EXPORT_PRIVATE EVRootCAMetadata {
 public:
#if defined(USE_NSS_CERTS)
  typedef SECOidTag PolicyOID;
#elif defined(OS_WIN)
  typedef const char* PolicyOID;
#elif defined(OS_MACOSX)
  // DER-encoded OID value (no tag or length).
  typedef der::Input PolicyOID;
#endif

  static EVRootCAMetadata* GetInstance();

#if defined(USE_NSS_CERTS) || defined(OS_WIN) || defined(OS_MACOSX)
  // Returns true if policy_oid is an EV policy OID of some root CA.
  bool IsEVPolicyOID(PolicyOID policy_oid) const;

  // Returns true if the root CA with the given certificate fingerprint has
  // the EV policy OID policy_oid.
  bool HasEVPolicyOID(const SHA256HashValue& fingerprint,
                      PolicyOID policy_oid) const;

  // Returns true if |policy_oid| is for 2.23.140.1.1 (CA/Browser Forum's
  // Extended Validation Policy).
  // TODO(eroman): Remove this and instead test each candidate OID.
  static bool IsCaBrowserForumEvOid(PolicyOID policy_oid);
#endif

  // AddEVCA adds an EV CA to the list of known EV CAs with the given policy.
  // |policy| is expressed as a string of dotted numbers. It returns true on
  // success.
  bool AddEVCA(const SHA256HashValue& fingerprint, const char* policy);

  // RemoveEVCA removes an EV CA that was previously added by AddEVCA. It
  // returns true on success.
  bool RemoveEVCA(const SHA256HashValue& fingerprint);

 private:
  friend struct base::LazyInstanceTraitsBase<EVRootCAMetadata>;

  EVRootCAMetadata();
  ~EVRootCAMetadata();

#if defined(USE_NSS_CERTS)
  using PolicyOIDMap = std::
      map<SHA256HashValue, std::vector<PolicyOID>, SHA256HashValueLessThan>;

  // RegisterOID registers |policy|, a policy OID in dotted string form, and
  // writes the memoized form to |*out|. It returns true on success.
  static bool RegisterOID(const char* policy, PolicyOID* out);

  PolicyOIDMap ev_policy_;
  std::set<PolicyOID> policy_oids_;
#elif defined(OS_WIN)
  using ExtraEVCAMap =
      std::map<SHA256HashValue, std::string, SHA256HashValueLessThan>;

  // extra_cas_ contains any EV CA metadata that was added at runtime.
  ExtraEVCAMap extra_cas_;
#elif defined(OS_MACOSX)
  using PolicyOIDMap = std::
      map<SHA256HashValue, std::vector<std::string>, SHA256HashValueLessThan>;

  PolicyOIDMap ev_policy_;
  std::set<std::string> policy_oids_;
#endif

  DISALLOW_COPY_AND_ASSIGN(EVRootCAMetadata);
};

}  // namespace net

#endif  // NET_CERT_EV_ROOT_CA_METADATA_H_
