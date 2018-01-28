// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_info.h"

#include "base/pickle.h"
#include "base/stl_util.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/ct_policy_status.h"
#include "net/cert/signed_certificate_timestamp.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace net {

SSLInfo::SSLInfo() {
  Reset();
}

SSLInfo::SSLInfo(const SSLInfo& info) {
  *this = info;
}

SSLInfo::~SSLInfo() {
}

SSLInfo& SSLInfo::operator=(const SSLInfo& info) {
  cert = info.cert;
  unverified_cert = info.unverified_cert;
  cert_status = info.cert_status;
  security_bits = info.security_bits;
  key_exchange_group = info.key_exchange_group;
  connection_status = info.connection_status;
  is_issued_by_known_root = info.is_issued_by_known_root;
  pkp_bypassed = info.pkp_bypassed;
  client_cert_sent = info.client_cert_sent;
  channel_id_sent = info.channel_id_sent;
  token_binding_negotiated = info.token_binding_negotiated;
  token_binding_key_param = info.token_binding_key_param;
  handshake_type = info.handshake_type;
  public_key_hashes = info.public_key_hashes;
  pinning_failure_log = info.pinning_failure_log;
  signed_certificate_timestamps = info.signed_certificate_timestamps;
  ct_compliance_details_available = info.ct_compliance_details_available;
  ct_cert_policy_compliance = info.ct_cert_policy_compliance;
  ocsp_result = info.ocsp_result;
  return *this;
}

void SSLInfo::Reset() {
  cert = NULL;
  unverified_cert = NULL;
  cert_status = 0;
  security_bits = -1;
  key_exchange_group = 0;
  connection_status = 0;
  is_issued_by_known_root = false;
  pkp_bypassed = false;
  client_cert_sent = false;
  channel_id_sent = false;
  token_binding_negotiated = false;
  token_binding_key_param = TB_PARAM_ECDSAP256;
  handshake_type = HANDSHAKE_UNKNOWN;
  base::STLClearObject(&public_key_hashes);
  base::STLClearObject(&pinning_failure_log);
  base::STLClearObject(&signed_certificate_timestamps);
  ct_compliance_details_available = false;
  ct_cert_policy_compliance =
      ct::CertPolicyCompliance::CERT_POLICY_COMPLIES_VIA_SCTS;
  ocsp_result = OCSPVerifyResult();
}

void SSLInfo::SetCertError(int error) {
  cert_status |= MapNetErrorToCertStatus(error);
}

void SSLInfo::UpdateCertificateTransparencyInfo(
    const ct::CTVerifyResult& ct_verify_result) {
  signed_certificate_timestamps.insert(signed_certificate_timestamps.end(),
                                       ct_verify_result.scts.begin(),
                                       ct_verify_result.scts.end());

  ct_compliance_details_available = ct_verify_result.ct_policies_applied;
  ct_cert_policy_compliance = ct_verify_result.cert_policy_compliance;
}

}  // namespace net
