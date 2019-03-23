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
#include "base/containers/span.h"
#include "base/strings/string_piece.h"
#include "net/ssl/ssl_client_cert_type.h"

// Misc functions to access the Android platform KeyStore.

namespace net {
namespace android {

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

// Returns the name of the class which implements the private key.
std::string GetPrivateKeyClassName(const base::android::JavaRef<jobject>& key);

// Compute the signature of a given input using a private key. For more
// details, please read the comments for the signWithPrivateKey method in
// AndroidKeyStore.java.
//
// |private_key| is a JNI reference for the private key.
// |algorithm| is the name of the algorithm to sign.
// |input| is the input to sign.
// |signature| will receive the signature on success.
// Returns true on success, false on failure.
bool SignWithPrivateKey(const base::android::JavaRef<jobject>& private_key,
                        base::StringPiece algorithm,
                        base::span<const uint8_t> input,
                        std::vector<uint8_t>* signature);

}  // namespace android
}  // namespace net

#endif  // NET_ANDROID_KEYSTORE_H_
