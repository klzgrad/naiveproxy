// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_ANDROID_KEYSTORE_H_
#define NET_ANDROID_KEYSTORE_H_

#include <jni.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "base/strings/string_piece.h"
#include "net/base/net_export.h"
#include "net/ssl/ssl_client_cert_type.h"

// Misc functions to access the Android platform KeyStore.

namespace net {
namespace android {

struct AndroidEVP_PKEY;

// Define a list of constants describing private key types. The
// values are shared with Java through org.chromium.net.PrivateKeyType.
// Example: PRIVATE_KEY_TYPE_RSA.
//
// A Java counterpart will be generated for this enum.
// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.net
enum PrivateKeyType {
  PRIVATE_KEY_TYPE_RSA = 0,
  // Obsolete: PRIVATE_KEY_TYPE_DSA = 1,
  PRIVATE_KEY_TYPE_ECDSA = 2,
  PRIVATE_KEY_TYPE_INVALID = 255,
};

// Compute the signature of a given message, which is actually a hash,
// using a private key. For more details, please read the comments for the
// rawSignDigestWithPrivateKey method in AndroidKeyStore.java.
//
// |private_key| is a JNI reference for the private key.
// |digest| is the input digest.
// |signature| will receive the signature on success.
// Returns true on success, false on failure.
//
NET_EXPORT bool RawSignDigestWithPrivateKey(
    const base::android::JavaRef<jobject>& private_key,
    const base::StringPiece& digest,
    std::vector<uint8_t>* signature);

// Returns a handle to the system AndroidEVP_PKEY object used to back a given
// private_key object. This must *only* be used for RSA private keys on Android
// < 4.2. Technically, this is only guaranteed to work if the system image
// contains a vanilla implementation of the Java API frameworks based on Harmony
// + OpenSSL.
//
// |private_key| is a JNI reference for the private key.
// Returns an AndroidEVP_PKEY* handle, or NULL in case of error.
//
// Note: Despite its name and return type, this function doesn't know
//       anything about OpenSSL, it just type-casts a system pointer that
//       is passed as an int through JNI. As such, it never increments
//       the returned key's reference count.
AndroidEVP_PKEY* GetOpenSSLSystemHandleForPrivateKey(
    const base::android::JavaRef<jobject>& private_key);

// Returns a JNI reference to the OpenSSLEngine object which is used to back a
// given private_key object. This must *only* be used for RSA private keys on
// Android < 4.2. Technically, this is only guaranteed to work if the system
// image contains a vanilla implementation of the Java API frameworks based on
// Harmony + OpenSSL.
base::android::ScopedJavaLocalRef<jobject> GetOpenSSLEngineForPrivateKey(
    const base::android::JavaRef<jobject>& private_key);

}  // namespace android
}  // namespace net

#endif  // NET_ANDROID_KEYSTORE_H_
