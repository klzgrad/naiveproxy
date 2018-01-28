// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_ANDROID_LEGACY_OPENSSL_H_
#define NET_ANDROID_LEGACY_OPENSSL_H_

// This file contains a replica of the Android system OpenSSL ABI shipped in
// Android 4.1.x (API level 16). The ABI may not necessarily be compatible with
// the copy of OpenSSL shipped in Chromium. This is used to implement
// RSA_private_encrypt in one of the legacy client auth codepaths.
//
// See https://android.googlesource.com/platform/external/openssl/+/android-4.1.2_r2.1

namespace net {
namespace android {

enum {
  ANDROID_EVP_PKEY_RSA = 6,
};

enum {
  ANDROID_RSA_PKCS1_PADDING = 1,
  ANDROID_RSA_SSLV23_PADDING = 2,
  ANDROID_RSA_NO_PADDING = 3,
  ANDROID_RSA_PKCS1_OAEP_PADDING = 4,
  ANDROID_X931_PADDING = 5,
  ANDROID_PKCS1_PSS_PADDING = 6,
};

struct AndroidEVP_PKEY_ASN1_METHOD;
struct AndroidRSA_METHOD;
struct AndroidSTACK;

struct AndroidCRYPTO_EX_DATA {
  AndroidSTACK* sk;
  int dummy;
};

struct AndroidENGINE {
  const char* id;
  // Remaining fields intentionally omitted.
};

struct AndroidRSA {
  int pad;
  long version;
  const AndroidRSA_METHOD* meth;
  AndroidENGINE* engine;
  // Remaining fields intentionally omitted.
};

struct AndroidRSA_METHOD {
  const char* name;
  int (*rsa_pub_enc)(int flen,
                     const unsigned char* from,
                     unsigned char* to,
                     AndroidRSA* rsa,
                     int padding);
  int (*rsa_pub_dec)(int flen,
                     const unsigned char* from,
                     unsigned char* to,
                     AndroidRSA* rsa,
                     int padding);
  int (*rsa_priv_enc)(int flen,
                      const unsigned char* from,
                      unsigned char* to,
                      AndroidRSA* rsa,
                      int padding);
  int (*rsa_priv_dec)(int flen,
                      const unsigned char* from,
                      unsigned char* to,
                      AndroidRSA* rsa,
                      int padding);
  // Remaining fields intentionally omitted.
};

struct AndroidEVP_PKEY {
  int type;
  int save_type;
  // Note: this value is protected by threading functions in the Android system
  // OpenSSL. It should not be accessed or modified directly.
  int references;
  const AndroidEVP_PKEY_ASN1_METHOD* ameth;
  AndroidENGINE* engine;
  union {
    char* ptr;
    AndroidRSA* rsa;
  } pkey;
  int save_parameters;
  AndroidSTACK* attributes;
};

}  // namespace android
}  // namespace net

#endif  // NET_ANDROID_LEGACY_OPENSSL_H_
