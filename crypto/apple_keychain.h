// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_APPLE_KEYCHAIN_H_
#define CRYPTO_APPLE_KEYCHAIN_H_

#include <Security/Security.h>

#include "base/macros.h"
#include "build/build_config.h"
#include "crypto/crypto_export.h"

#if defined (OS_IOS)
typedef void* SecKeychainRef;
typedef void* SecKeychainItemRef;
typedef void SecKeychainAttributeList;
#endif

namespace crypto {

// Wraps the KeychainServices API in a very thin layer, to allow it to be
// mocked out for testing.

// See Keychain Services documentation for function documentation, as these call
// through directly to their Keychain Services equivalents (Foo ->
// SecKeychainFoo). The only exception is Free, which should be used for
// anything returned from this class that would normally be freed with
// CFRelease (to aid in testing).
class CRYPTO_EXPORT AppleKeychain {
 public:
  AppleKeychain();
  virtual ~AppleKeychain();

  virtual OSStatus FindGenericPassword(CFTypeRef keychainOrArray,
                                       UInt32 serviceNameLength,
                                       const char* serviceName,
                                       UInt32 accountNameLength,
                                       const char* accountName,
                                       UInt32* passwordLength,
                                       void** passwordData,
                                       SecKeychainItemRef* itemRef) const;

  virtual OSStatus ItemFreeContent(SecKeychainAttributeList* attrList,
                                   void* data) const;

  virtual OSStatus AddGenericPassword(SecKeychainRef keychain,
                                      UInt32 serviceNameLength,
                                      const char* serviceName,
                                      UInt32 accountNameLength,
                                      const char* accountName,
                                      UInt32 passwordLength,
                                      const void* passwordData,
                                      SecKeychainItemRef* itemRef) const;

#if !defined(OS_IOS)
  virtual OSStatus ItemDelete(SecKeychainItemRef itemRef) const;
#endif  // !defined(OS_IOS)

 private:
  DISALLOW_COPY_AND_ASSIGN(AppleKeychain);
};

}  // namespace crypto

#endif  // CRYPTO_APPLE_KEYCHAIN_H_
