// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/http_auth_negotiate_android.h"

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "jni/HttpNegotiateAuthenticator_jni.h"
#include "net/base/auth.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_auth_multi_round_parse.h"
#include "net/http/http_auth_preferences.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF8ToJavaString;
using base::android::ConvertJavaStringToUTF8;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace net {
namespace android {

JavaNegotiateResultWrapper::JavaNegotiateResultWrapper(
    const scoped_refptr<base::TaskRunner>& callback_task_runner,
    const base::Callback<void(int, const std::string&)>& thread_safe_callback)
    : callback_task_runner_(callback_task_runner),
      thread_safe_callback_(thread_safe_callback) {
}

JavaNegotiateResultWrapper::~JavaNegotiateResultWrapper() {
}

void JavaNegotiateResultWrapper::SetResult(JNIEnv* env,
                                           const JavaParamRef<jobject>& obj,
                                           int result,
                                           const JavaParamRef<jstring>& token) {
  // This will be called on the UI thread, so we have to post a task back to the
  // correct thread to actually save the result
  std::string raw_token = ConvertJavaStringToUTF8(env, token);
  // Always post, even if we are on the same thread. This guarantees that the
  // result will be delayed until after the request has completed, which
  // simplifies the logic. In practice the result will only ever come back on
  // the original thread in an obscure error case.
  callback_task_runner_->PostTask(
      FROM_HERE, base::Bind(thread_safe_callback_, result, raw_token));
  // We will always get precisely one call to set result for each call to
  // getNextAuthToken, so we can now delete the callback object, and must
  // do so to avoid a memory leak.
  delete this;
}

HttpAuthNegotiateAndroid::HttpAuthNegotiateAndroid(
    const HttpAuthPreferences* prefs)
    : prefs_(prefs),
      can_delegate_(false),
      first_challenge_(true),
      auth_token_(nullptr),
      weak_factory_(this) {
  JNIEnv* env = AttachCurrentThread();
  java_authenticator_.Reset(Java_HttpNegotiateAuthenticator_create(
      env,
      ConvertUTF8ToJavaString(env, prefs->AuthAndroidNegotiateAccountType())));
}

HttpAuthNegotiateAndroid::~HttpAuthNegotiateAndroid() {
}

bool HttpAuthNegotiateAndroid::Init() {
  return true;
}

bool HttpAuthNegotiateAndroid::NeedsIdentity() const {
  return false;
}

bool HttpAuthNegotiateAndroid::AllowsExplicitCredentials() const {
  return false;
}

HttpAuth::AuthorizationResult HttpAuthNegotiateAndroid::ParseChallenge(
    net::HttpAuthChallengeTokenizer* tok) {
  if (first_challenge_) {
    first_challenge_ = false;
    return net::ParseFirstRoundChallenge("negotiate", tok);
  }
  std::string decoded_auth_token;
  return net::ParseLaterRoundChallenge("negotiate", tok, &server_auth_token_,
                                       &decoded_auth_token);
}

int HttpAuthNegotiateAndroid::GenerateAuthToken(
    const AuthCredentials* credentials,
    const std::string& spn,
    const std::string& channel_bindings,
    std::string* auth_token,
    const net::CompletionCallback& callback) {
  if (prefs_->AuthAndroidNegotiateAccountType().empty()) {
    // This can happen if there is a policy change, removing the account type,
    // in the middle of a negotiation.
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  }
  DCHECK(auth_token);
  DCHECK(completion_callback_.is_null());
  DCHECK(!callback.is_null());

  auth_token_ = auth_token;
  completion_callback_ = callback;
  scoped_refptr<base::SingleThreadTaskRunner> callback_task_runner =
      base::ThreadTaskRunnerHandle::Get();
  base::Callback<void(int, const std::string&)> thread_safe_callback =
      base::Bind(&HttpAuthNegotiateAndroid::SetResultInternal,
                 weak_factory_.GetWeakPtr());
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> java_server_auth_token =
      ConvertUTF8ToJavaString(env, server_auth_token_);
  ScopedJavaLocalRef<jstring> java_spn = ConvertUTF8ToJavaString(env, spn);
  ScopedJavaLocalRef<jstring> java_account_type =
      ConvertUTF8ToJavaString(env, prefs_->AuthAndroidNegotiateAccountType());

  // It is intentional that callback_wrapper is not owned or deleted by the
  // HttpAuthNegotiateAndroid object. The Java code will call the callback
  // asynchronously on a different thread, and needs an object to call it on. As
  // such, the callback_wrapper must not be deleted until the callback has been
  // called, whatever happens to the HttpAuthNegotiateAndroid object.
  //
  // Unfortunately we have no automated way of managing C++ objects owned by
  // Java, so the Java code must simply be written to guarantee that the
  // callback is, in the end, called.
  JavaNegotiateResultWrapper* callback_wrapper = new JavaNegotiateResultWrapper(
      callback_task_runner, thread_safe_callback);
  Java_HttpNegotiateAuthenticator_getNextAuthToken(
      env, java_authenticator_, reinterpret_cast<intptr_t>(callback_wrapper),
      java_spn, java_server_auth_token, can_delegate_);
  return ERR_IO_PENDING;
}

void HttpAuthNegotiateAndroid::Delegate() {
  can_delegate_ = true;
}

void HttpAuthNegotiateAndroid::SetResultInternal(int result,
                                                 const std::string& raw_token) {
  DCHECK(auth_token_);
  DCHECK(!completion_callback_.is_null());
  if (result == OK)
    *auth_token_ = "Negotiate " + raw_token;
  base::ResetAndReturn(&completion_callback_).Run(result);
}

}  // namespace android
}  // namespace net
