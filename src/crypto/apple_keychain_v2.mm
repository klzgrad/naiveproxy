// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Foundation/Foundation.h>

#include "crypto/apple_keychain_v2.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/no_destructor.h"

namespace crypto {

static AppleKeychainV2* g_keychain_instance_override = nullptr;

// static
AppleKeychainV2& AppleKeychainV2::GetInstance() {
  if (g_keychain_instance_override) {
    return *g_keychain_instance_override;
  }
  static base::NoDestructor<AppleKeychainV2> k;
  return *k;
}

// static
void AppleKeychainV2::SetInstanceOverride(AppleKeychainV2* AppleKeychainV2) {
  CHECK(!g_keychain_instance_override);
  g_keychain_instance_override = AppleKeychainV2;
}

// static
void AppleKeychainV2::ClearInstanceOverride() {
  CHECK(g_keychain_instance_override);
  g_keychain_instance_override = nullptr;
}

AppleKeychainV2::AppleKeychainV2() = default;
AppleKeychainV2::~AppleKeychainV2() = default;

base::apple::ScopedCFTypeRef<SecKeyRef> AppleKeychainV2::KeyCreateRandomKey(
    CFDictionaryRef params,
    CFErrorRef* error) {
  return base::apple::ScopedCFTypeRef<SecKeyRef>(
      SecKeyCreateRandomKey(params, error));
}

base::apple::ScopedCFTypeRef<CFDataRef> AppleKeychainV2::KeyCreateSignature(
    SecKeyRef key,
    SecKeyAlgorithm algorithm,
    CFDataRef data,
    CFErrorRef* error) {
  return base::apple::ScopedCFTypeRef<CFDataRef>(
      SecKeyCreateSignature(key, algorithm, data, error));
}

base::apple::ScopedCFTypeRef<SecKeyRef> AppleKeychainV2::KeyCopyPublicKey(
    SecKeyRef key) {
  return base::apple::ScopedCFTypeRef<SecKeyRef>(SecKeyCopyPublicKey(key));
}

OSStatus AppleKeychainV2::ItemCopyMatching(
    CFDictionaryRef query, CFTypeRef* result) {
  return SecItemCopyMatching(query, result);
}

OSStatus AppleKeychainV2::ItemDelete(CFDictionaryRef query) {
  return SecItemDelete(query);
}

OSStatus AppleKeychainV2::ItemUpdate(
    CFDictionaryRef query,
    base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> keychain_data) {
  return SecItemUpdate(query, keychain_data.get());
}

}  // namespace crypto
