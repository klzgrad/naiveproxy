// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_ANDROID_HTTP_AUTH_NEGOTIATE_ANDROID_H_
#define NET_ANDROID_HTTP_AUTH_NEGOTIATE_ANDROID_H_

#include <jni.h>

#include <memory>
#include <string>

#include "base/android/jni_android.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/http/http_auth.h"

namespace base {
class TaskRunner;
}

namespace net {

class HttpAuthChallengeTokenizer;
class HttpAuthPreferences;

namespace android {

// This class provides a threadsafe wrapper for SetResult, which is called from
// Java. A new instance of this class is needed for each call, and the instance
// destroys itself when the callback is received. It is written to allow
// setResult to be called on any thread, but in practice they will be called
// on the application's main thread.
//
// We cannot use a Callback object here, because there is no way of invoking the
// Run method from Java.
class NET_EXPORT_PRIVATE JavaNegotiateResultWrapper {
 public:
  scoped_refptr<base::TaskRunner> callback_task_runner_;
  base::Callback<void(int, const std::string&)> thread_safe_callback_;

  JavaNegotiateResultWrapper(
      const scoped_refptr<base::TaskRunner>& callback_task_runner,
      const base::Callback<void(int, const std::string&)>&
          thread_safe_callback);

  void SetResult(JNIEnv* env,
                 const base::android::JavaParamRef<jobject>& obj,
                 int result,
                 const base::android::JavaParamRef<jstring>& token);

 private:
  // Class is only allowed to delete itself, nobody else is allowed to delete.
  ~JavaNegotiateResultWrapper();
};

// Class providing Negotiate (SPNEGO/Kerberos) authentication support on
// Android. The actual authentication is done through an Android authenticator
// provided by third parties who want Kerberos support. This class simply
// provides a bridge to the Java code, and hence to the service. See
// https://drive.google.com/open?id=1G7WAaYEKMzj16PTHT_cIYuKXJG6bBcrQ7QQBQ6ihOcQ&authuser=1
// for the full details.
class NET_EXPORT_PRIVATE HttpAuthNegotiateAndroid {
 public:
  // Creates an object for one negotiation session. |prefs| are the
  // authentication preferences. In particular they include the Android account
  // type, which is used to connect to the correct Android Authenticator.
  explicit HttpAuthNegotiateAndroid(const HttpAuthPreferences* prefs);
  ~HttpAuthNegotiateAndroid();

  // Does nothing, but needed for compatibility with the Negotiate
  // authenticators for other O.S.. Always returns true.
  bool Init();

  // True if authentication needs the identity of the user from Chrome.
  bool NeedsIdentity() const;

  // True authentication can use explicit credentials included in the URL.
  bool AllowsExplicitCredentials() const;

  // Parse a received Negotiate challenge.
  HttpAuth::AuthorizationResult ParseChallenge(
      net::HttpAuthChallengeTokenizer* tok);

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
  // If the AndroidAuthNegotiate object is deleted before completion then the
  // callback will not be called.
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
                        const net::CompletionCallback& callback);

  // Delegation is allowed on the Kerberos ticket. This allows certain servers
  // to act as the user, such as an IIS server retrieving data from a
  // Kerberized MSSQL server.
  void Delegate();

 private:
  void SetResultInternal(int result, const std::string& token);

  const HttpAuthPreferences* prefs_;
  bool can_delegate_;
  bool first_challenge_;
  std::string server_auth_token_;
  std::string* auth_token_;
  base::android::ScopedJavaGlobalRef<jobject> java_authenticator_;
  net::CompletionCallback completion_callback_;

  base::WeakPtrFactory<HttpAuthNegotiateAndroid> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(HttpAuthNegotiateAndroid);
};

}  // namespace android
}  // namespace net

#endif  // NET_ANDROID_HTTP_AUTH_NEGOTIATE_ANDROID_H_
