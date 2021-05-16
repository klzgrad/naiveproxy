// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/network_library.h"

#include "net/base/net_errors.h"

namespace net {
namespace android {

void VerifyX509CertChain(const std::vector<std::string>& cert_chain,
                         base::StringPiece auth_type,
                         base::StringPiece host,
                         CertVerifyStatusAndroid* status,
                         bool* is_issued_by_known_root,
                         std::vector<std::string>* verified_chain) {
}

void AddTestRootCertificate(const uint8_t* cert, size_t len) {
}

void ClearTestRootCertificates() {
}

bool IsCleartextPermitted(const std::string& host) {
  return true;
}

bool HaveOnlyLoopbackAddresses() {
  return false;
}

bool GetMimeTypeFromExtension(const std::string& extension,
                              std::string* result) {
  return false;
}

std::string GetTelephonyNetworkOperator() {
  return {};
}

std::string GetTelephonySimOperator() {
  return {};
}

bool GetIsRoaming() {
  return false;
}

bool GetIsCaptivePortal() {
  return false;
}

std::string GetWifiSSID() {
  return {};
}

absl::optional<int32_t> GetWifiSignalLevel() {
  return {};
}

bool GetCurrentDnsServers(std::vector<IPEndPoint>* dns_servers,
                          bool* dns_over_tls_active,
                          std::string* dns_over_tls_hostname,
                          std::vector<std::string>* search_suffixes) {
  return false;
}

bool GetDnsServersForNetwork(std::vector<IPEndPoint>* dns_servers,
                             bool* dns_over_tls_active,
                             std::string* dns_over_tls_hostname,
                             std::vector<std::string>* search_suffixes,
                             handles::NetworkHandle network) {
  return false;
}

bool ReportBadDefaultNetwork() {
  return false;
}

void TagSocket(SocketDescriptor socket, uid_t uid, int32_t tag) {
}

int BindToNetwork(SocketDescriptor socket, handles::NetworkHandle network) {
  return ERR_NOT_IMPLEMENTED;
}

int GetAddrInfoForNetwork(handles::NetworkHandle network,
                          const char* node,
                          const char* service,
                          const struct addrinfo* hints,
                          struct addrinfo** res) {
  return EAI_SYSTEM;
}

}  // namespace android
}  // namespace net
