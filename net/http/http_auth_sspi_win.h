// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains common routines used by NTLM and Negotiate authentication
// using the SSPI API on Windows.

#ifndef NET_HTTP_HTTP_AUTH_SSPI_WIN_H_
#define NET_HTTP_HTTP_AUTH_SSPI_WIN_H_

// security.h needs to be included for CredHandle. Unfortunately CredHandle
// is a typedef and can't be forward declared.
#define SECURITY_WIN32 1
#include <windows.h>
#include <security.h>

#include <string>

#include "base/strings/string16.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/http/http_auth.h"

namespace net {

class HttpAuthChallengeTokenizer;

// SSPILibrary is introduced so unit tests can mock the calls to Windows' SSPI
// implementation. The default implementation simply passes the arguments on to
// the SSPI implementation provided by Secur32.dll.
// NOTE(cbentzel): I considered replacing the Secur32.dll with a mock DLL, but
// decided that it wasn't worth the effort as this is unlikely to be performance
// sensitive code.
class SSPILibrary {
 public:
  virtual ~SSPILibrary() {}

  virtual SECURITY_STATUS AcquireCredentialsHandle(LPWSTR pszPrincipal,
                                                   LPWSTR pszPackage,
                                                   unsigned long fCredentialUse,
                                                   void* pvLogonId,
                                                   void* pvAuthData,
                                                   SEC_GET_KEY_FN pGetKeyFn,
                                                   void* pvGetKeyArgument,
                                                   PCredHandle phCredential,
                                                   PTimeStamp ptsExpiry) = 0;

  virtual SECURITY_STATUS InitializeSecurityContext(PCredHandle phCredential,
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
                                                    PTimeStamp ptsExpiry) = 0;

  virtual SECURITY_STATUS QuerySecurityPackageInfo(LPWSTR pszPackageName,
                                                   PSecPkgInfoW *pkgInfo) = 0;

  virtual SECURITY_STATUS FreeCredentialsHandle(PCredHandle phCredential) = 0;

  virtual SECURITY_STATUS DeleteSecurityContext(PCtxtHandle phContext) = 0;

  virtual SECURITY_STATUS FreeContextBuffer(PVOID pvContextBuffer) = 0;
};

class SSPILibraryDefault : public SSPILibrary {
 public:
  SSPILibraryDefault() {}
  ~SSPILibraryDefault() override {}

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
};

class NET_EXPORT_PRIVATE HttpAuthSSPI {
 public:
  HttpAuthSSPI(SSPILibrary* sspi_library,
               const std::string& scheme,
               const SEC_WCHAR* security_package,
               ULONG max_token_length);
  ~HttpAuthSSPI();

  bool NeedsIdentity() const;

  bool AllowsExplicitCredentials() const;

  HttpAuth::AuthorizationResult ParseChallenge(
      HttpAuthChallengeTokenizer* tok);

  // Generates an authentication token.
  //
  // The return value is an error code. The authentication token will be
  // returned in |*auth_token|. If the result code is not |OK|, the value of
  // |*auth_token| is unspecified.
  //
  // If the operation cannot be completed synchronously, |ERR_IO_PENDING| will
  // be returned and the real result code will be passed to the completion
  // callback.  Otherwise the result code is returned immediately from this
  // call.
  //
  // If the HttpAuthSPPI object is deleted before completion then the callback
  // will not be called.
  //
  // If no immediate result is returned then |auth_token| must remain valid
  // until the callback has been called.
  //
  // |spn| is the Service Principal Name of the server that the token is
  // being generated for.
  //
  // If this is the first round of a multiple round scheme, credentials are
  // obtained using |*credentials|. If |credentials| is NULL, the default
  // credentials are used instead.
  int GenerateAuthToken(const AuthCredentials* credentials,
                        const std::string& spn,
                        const std::string& channel_bindings,
                        std::string* auth_token,
                        const CompletionCallback& callback);

  // Delegation is allowed on the Kerberos ticket. This allows certain servers
  // to act as the user, such as an IIS server retrieving data from a
  // Kerberized MSSQL server.
  void Delegate();

 private:
  int OnFirstRound(const AuthCredentials* credentials);

  int GetNextSecurityToken(const std::string& spn,
                           const std::string& channing_bindings,
                           const void* in_token,
                           int in_token_len,
                           void** out_token,
                           int* out_token_len);

  void ResetSecurityContext();

  SSPILibrary* library_;
  std::string scheme_;
  const SEC_WCHAR* security_package_;
  std::string decoded_server_auth_token_;
  ULONG max_token_length_;
  CredHandle cred_;
  CtxtHandle ctxt_;
  bool can_delegate_;
};

// Splits |combined| into domain and username.
// If |combined| is of form "FOO\bar", |domain| will contain "FOO" and |user|
// will contain "bar".
// If |combined| is of form "bar", |domain| will be empty and |user| will
// contain "bar".
// |domain| and |user| must be non-NULL.
NET_EXPORT_PRIVATE void SplitDomainAndUser(const base::string16& combined,
                                           base::string16* domain,
                                           base::string16* user);

// Determines the maximum token length in bytes for a particular SSPI package.
//
// |library| and |max_token_length| must be non-NULL pointers to valid objects.
//
// If the return value is OK, |*max_token_length| contains the maximum token
// length in bytes.
//
// If the return value is ERR_UNSUPPORTED_AUTH_SCHEME, |package| is not an
// known SSPI authentication scheme on this system. |*max_token_length| is not
// changed.
//
// If the return value is ERR_UNEXPECTED, there was an unanticipated problem
// in the underlying SSPI call. The details are logged, and |*max_token_length|
// is not changed.
NET_EXPORT_PRIVATE int DetermineMaxTokenLength(SSPILibrary* library,
                                               const std::wstring& package,
                                               ULONG* max_token_length);

}  // namespace net

#endif  // NET_HTTP_HTTP_AUTH_SSPI_WIN_H_
