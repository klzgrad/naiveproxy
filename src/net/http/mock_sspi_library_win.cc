// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/mock_sspi_library_win.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace net {

MockSSPILibrary::MockSSPILibrary() {
}

MockSSPILibrary::~MockSSPILibrary() {
  EXPECT_TRUE(expected_package_queries_.empty());
  EXPECT_TRUE(expected_freed_packages_.empty());
}

SECURITY_STATUS MockSSPILibrary::AcquireCredentialsHandle(
    LPWSTR pszPrincipal,
    LPWSTR pszPackage,
    unsigned long fCredentialUse,
    void* pvLogonId,
    void* pvAuthData,
    SEC_GET_KEY_FN pGetKeyFn,
    void* pvGetKeyArgument,
    PCredHandle phCredential,
    PTimeStamp ptsExpiry) {
  // Fill in phCredential with arbitrary value.
  phCredential->dwLower = phCredential->dwUpper = ((ULONG_PTR) ((INT_PTR)0));
  return SEC_E_OK;
}

SECURITY_STATUS MockSSPILibrary::InitializeSecurityContext(
    PCredHandle phCredential,
    PCtxtHandle phContext,
    SEC_WCHAR* pszTargetName,
    unsigned long fContextReq,
    unsigned long Reserved1,
    unsigned long TargetDataRep,
    PSecBufferDesc pInput,
    unsigned long Reserved2,
    PCtxtHandle phNewContext,
    PSecBufferDesc pOutput,
    unsigned long* contextAttr,
    PTimeStamp ptsExpiry) {
  // Fill in the outbound buffer with garbage data.
  PSecBuffer out_buffer = pOutput->pBuffers;
  out_buffer->cbBuffer = 2;
  uint8_t* buf = reinterpret_cast<uint8_t*>(out_buffer->pvBuffer);
  buf[0] = 0xAB;
  buf[1] = 0xBA;

  // Fill in phNewContext with arbitrary value if it's invalid.
  if (phNewContext != phContext)
    phNewContext->dwLower = phNewContext->dwUpper = ((ULONG_PTR) ((INT_PTR)0));
  return SEC_E_OK;
}

SECURITY_STATUS MockSSPILibrary::QuerySecurityPackageInfo(
    LPWSTR pszPackageName, PSecPkgInfoW *pkgInfo) {
  EXPECT_TRUE(!expected_package_queries_.empty());
  PackageQuery package_query = expected_package_queries_.front();
  expected_package_queries_.pop_front();
  std::wstring actual_package(pszPackageName);
  EXPECT_EQ(package_query.expected_package, actual_package);
  *pkgInfo = package_query.package_info;
  if (package_query.response_code == SEC_E_OK)
    expected_freed_packages_.insert(package_query.package_info);
  return package_query.response_code;
}

SECURITY_STATUS MockSSPILibrary::FreeCredentialsHandle(
    PCredHandle phCredential) {
  EXPECT_TRUE(phCredential->dwLower == ((ULONG_PTR) ((INT_PTR) 0)));
  EXPECT_TRUE(phCredential->dwUpper == ((ULONG_PTR) ((INT_PTR) 0)));
  SecInvalidateHandle(phCredential);
  return SEC_E_OK;
}

SECURITY_STATUS MockSSPILibrary::DeleteSecurityContext(PCtxtHandle phContext) {
  EXPECT_TRUE(phContext->dwLower == ((ULONG_PTR) ((INT_PTR) 0)));
  EXPECT_TRUE(phContext->dwUpper == ((ULONG_PTR) ((INT_PTR) 0)));
  SecInvalidateHandle(phContext);
  return SEC_E_OK;
}

SECURITY_STATUS MockSSPILibrary::FreeContextBuffer(PVOID pvContextBuffer) {
  PSecPkgInfoW package_info = static_cast<PSecPkgInfoW>(pvContextBuffer);
  std::set<PSecPkgInfoW>::iterator it = expected_freed_packages_.find(
      package_info);
  EXPECT_TRUE(it != expected_freed_packages_.end());
  expected_freed_packages_.erase(it);
  return SEC_E_OK;
}

void MockSSPILibrary::ExpectQuerySecurityPackageInfo(
    const std::wstring& expected_package,
    SECURITY_STATUS response_code,
    PSecPkgInfoW package_info) {
  PackageQuery package_query = {expected_package, response_code,
                                package_info};
  expected_package_queries_.push_back(package_query);
}

}  // namespace net
