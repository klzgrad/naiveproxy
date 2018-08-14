// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_MOCK_SSPI_LIBRARY_WIN_H_
#define NET_HTTP_MOCK_SSPI_LIBRARY_WIN_H_

#include <list>
#include <set>

#include "net/http/http_auth_sspi_win.h"

namespace net {

// The MockSSPILibrary class is intended for unit tests which want to bypass
// the system SSPI library calls.
class MockSSPILibrary : public SSPILibrary {
 public:
  MockSSPILibrary();
  ~MockSSPILibrary() override;

  // TODO(cbentzel): Only QuerySecurityPackageInfo and FreeContextBuffer
  //                 are properly handled currently.
  // SSPILibrary methods:
  SECURITY_STATUS AcquireCredentialsHandle(LPWSTR pszPrincipal,
                                           LPWSTR pszPackage,
                                           unsigned long fCredentialUse,
                                           void* pvLogonId,
                                           void* pvAuthData,
                                           SEC_GET_KEY_FN pGetKeyFn,
                                           void* pvGetKeyArgument,
                                           PCredHandle phCredential,
                                           PTimeStamp ptsExpiry) override;
  SECURITY_STATUS InitializeSecurityContext(PCredHandle phCredential,
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
                                            PTimeStamp ptsExpiry) override;
  SECURITY_STATUS QuerySecurityPackageInfo(LPWSTR pszPackageName,
                                           PSecPkgInfoW* pkgInfo) override;
  SECURITY_STATUS FreeCredentialsHandle(PCredHandle phCredential) override;
  SECURITY_STATUS DeleteSecurityContext(PCtxtHandle phContext) override;
  SECURITY_STATUS FreeContextBuffer(PVOID pvContextBuffer) override;

  // Establishes an expectation for a |QuerySecurityPackageInfo()| call.
  //
  // Each expectation established by |ExpectSecurityQueryPackageInfo()| must be
  // matched by a call to |QuerySecurityPackageInfo()| during the lifetime of
  // the MockSSPILibrary. The |expected_package| argument must equal the
  // |*pszPackageName| argument to |QuerySecurityPackageInfo()| for there to be
  // a match. The expectations also establish an explicit ordering.
  //
  // For example, this sequence will be successful.
  //   MockSSPILibrary lib;
  //   lib.ExpectQuerySecurityPackageInfo(L"NTLM", ...)
  //   lib.ExpectQuerySecurityPackageInfo(L"Negotiate", ...)
  //   lib.QuerySecurityPackageInfo(L"NTLM", ...)
  //   lib.QuerySecurityPackageInfo(L"Negotiate", ...)
  //
  // This sequence will fail since the queries do not occur in the order
  // established by the expectations.
  //   MockSSPILibrary lib;
  //   lib.ExpectQuerySecurityPackageInfo(L"NTLM", ...)
  //   lib.ExpectQuerySecurityPackageInfo(L"Negotiate", ...)
  //   lib.QuerySecurityPackageInfo(L"Negotiate", ...)
  //   lib.QuerySecurityPackageInfo(L"NTLM", ...)
  //
  // This sequence will fail because there were not enough queries.
  //   MockSSPILibrary lib;
  //   lib.ExpectQuerySecurityPackageInfo(L"NTLM", ...)
  //   lib.ExpectQuerySecurityPackageInfo(L"Negotiate", ...)
  //   lib.QuerySecurityPackageInfo(L"NTLM", ...)
  //
  // |response_code| is used as the return value for
  // |QuerySecurityPackageInfo()|. If |response_code| is SEC_E_OK,
  // an expectation is also set for a call to |FreeContextBuffer()| after
  // the matching |QuerySecurityPackageInfo()| is called.
  //
  // |package_info| is assigned to |*pkgInfo| in |QuerySecurityPackageInfo|.
  // The lifetime of |*package_info| should last at least until the matching
  // |QuerySecurityPackageInfo()| is called.
  void ExpectQuerySecurityPackageInfo(const std::wstring& expected_package,
                                      SECURITY_STATUS response_code,
                                      PSecPkgInfoW package_info);

 private:
  struct PackageQuery {
    std::wstring expected_package;
    SECURITY_STATUS response_code;
    PSecPkgInfoW package_info;
  };

  // expected_package_queries contains an ordered list of expected
  // |QuerySecurityPackageInfo()| calls and the return values for those
  // calls.
  std::list<PackageQuery> expected_package_queries_;

  // Set of packages which should be freed.
  std::set<PSecPkgInfoW> expected_freed_packages_;
};

}  // namespace net

#endif  // NET_HTTP_MOCK_SSPI_LIBRARY_WIN_H_
