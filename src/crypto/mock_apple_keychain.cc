// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/mock_apple_keychain.h"

#include "base/check_op.h"
#include "base/metrics/histogram_macros.h"
#include "base/time/time.h"

namespace {

// Adds an entry to a local histogram to indicate that the Apple Keychain would
// have been accessed, if this class were not a mock of the Apple Keychain.
void IncrementKeychainAccessHistogram() {
  // This local histogram is accessed by Telemetry to track the number of times
  // the keychain is accessed, since keychain access is known to be synchronous
  // and slow.
  LOCAL_HISTOGRAM_BOOLEAN("OSX.Keychain.Access", true);
}

}  // namespace

namespace crypto {

OSStatus MockAppleKeychain::FindGenericPassword(
    UInt32 serviceNameLength,
    const char* serviceName,
    UInt32 accountNameLength,
    const char* accountName,
    UInt32* passwordLength,
    void** passwordData,
    AppleSecKeychainItemRef* itemRef) const {
  IncrementKeychainAccessHistogram();

  // When simulating |noErr|, return canned |passwordData| and
  // |passwordLength|.  Otherwise, just return given code.
  if (find_generic_result_ == noErr) {
    static const char kPassword[] = "my_password";
    DCHECK(passwordData);
    // The function to free this data is mocked so the cast is fine.
    *passwordData = const_cast<char*>(kPassword);
    DCHECK(passwordLength);
    *passwordLength = std::size(kPassword);
    password_data_count_++;
  }

  return find_generic_result_;
}

OSStatus MockAppleKeychain::ItemFreeContent(void* data) const {
  // No-op.
  password_data_count_--;
  return noErr;
}

OSStatus MockAppleKeychain::AddGenericPassword(
    UInt32 serviceNameLength,
    const char* serviceName,
    UInt32 accountNameLength,
    const char* accountName,
    UInt32 passwordLength,
    const void* passwordData,
    AppleSecKeychainItemRef* itemRef) const {
  IncrementKeychainAccessHistogram();

  called_add_generic_ = true;

  DCHECK_GT(passwordLength, 0U);
  DCHECK(passwordData);
  return noErr;
}

std::string MockAppleKeychain::GetEncryptionPassword() const {
  IncrementKeychainAccessHistogram();
  return "mock_password";
}

}  // namespace crypto
