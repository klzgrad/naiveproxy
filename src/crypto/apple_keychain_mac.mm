// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/apple_keychain.h"

#import <Foundation/Foundation.h>

#include "base/synchronization/lock.h"
#include "crypto/mac_security_services_lock.h"

namespace crypto {

AppleKeychain::AppleKeychain() {}

AppleKeychain::~AppleKeychain() {}

OSStatus AppleKeychain::ItemDelete(AppleSecKeychainItemRef itemRef) const {
  base::AutoLock lock(GetMacSecurityServicesLock());
  return SecKeychainItemDelete(itemRef);
}

OSStatus AppleKeychain::FindGenericPassword(
    UInt32 serviceNameLength,
    const char* serviceName,
    UInt32 accountNameLength,
    const char* accountName,
    UInt32* passwordLength,
    void** passwordData,
    AppleSecKeychainItemRef* itemRef) const {
  base::AutoLock lock(GetMacSecurityServicesLock());
  return SecKeychainFindGenericPassword(nullptr, serviceNameLength, serviceName,
                                        accountNameLength, accountName,
                                        passwordLength, passwordData, itemRef);
}

OSStatus AppleKeychain::ItemFreeContent(void* data) const {
  base::AutoLock lock(GetMacSecurityServicesLock());
  return SecKeychainItemFreeContent(nullptr, data);
}

OSStatus AppleKeychain::AddGenericPassword(
    UInt32 serviceNameLength,
    const char* serviceName,
    UInt32 accountNameLength,
    const char* accountName,
    UInt32 passwordLength,
    const void* passwordData,
    AppleSecKeychainItemRef* itemRef) const {
  base::AutoLock lock(GetMacSecurityServicesLock());
  return SecKeychainAddGenericPassword(nullptr, serviceNameLength, serviceName,
                                       accountNameLength, accountName,
                                       passwordLength, passwordData, itemRef);
}

}  // namespace crypto
