// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains forward declarations for items in later SDKs than the
// default one with which Chromium is built (currently 10.10).
// If you call any function from this header, be sure to check at runtime for
// respondsToSelector: before calling these functions (else your code will crash
// on older OS X versions that chrome still supports).

#ifndef BASE_MAC_SDK_FORWARD_DECLARATIONS_H_
#define BASE_MAC_SDK_FORWARD_DECLARATIONS_H_

#import <AppKit/AppKit.h>
#include <AvailabilityMacros.h>
#include <os/availability.h>

// NOTE: If an #import is needed only for a newer SDK, it might be found below.

#include "base/base_export.h"

// ----------------------------------------------------------------------------
// Old symbols that used to be in the macOS SDK but are no longer.
// ----------------------------------------------------------------------------

// kCWSSIDDidChangeNotification is available in the CoreWLAN.framework for OSX
// versions 10.6 through 10.10 but stopped being included starting with the 10.9
// SDK. Remove when 10.10 is no longer supported by Chromium.
BASE_EXPORT extern "C" NSString* const kCWSSIDDidChangeNotification;

// ----------------------------------------------------------------------------
// Definitions from SDKs newer than the one that Chromium compiles against.
//
// HOW TO DO THIS:
//
// 1. In this file:
//   a. Use an #if !defined() guard
//   b. Include all API_AVAILABLE/NS_CLASS_AVAILABLE_MAC annotations
//   c. Optionally import frameworks
// 2. In your source file:
//   a. Correctly use @available to annotate availability
//
// This way, when the SDK is rolled, the section full of definitions
// corresponding to it can be easily deleted.
//
// EXAMPLES OF HOW TO DO THIS:
//
// Suppose there's a simple extension of NSApplication in macOS 10.25. Then:
//
//   #if !defined(MAC_OS_X_VERSION_10_25) || \
//       MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_25
//
//   @interface NSApplication (MacOSHouseCatSDK)
//   @property(readonly) CGFloat purrRate API_AVAILABLE(macos(10.25));
//   @end
//
//   #endif  // MAC_OS_X_VERSION_10_25
//
//
// Suppose the CoreShoelace framework is introduced in macOS 10.77. Then:
//
//   #if !defined(MAC_OS_X_VERSION_10_77) || \
//       MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_77
//
//   API_AVAILABLE(macos(10.77))
//   @interface NSCoreShoelace : NSObject
//   @property (readonly) NSUInteger stringLength;
//   @end
//
//   #else
//
//   #import <CoreShoelace/CoreShoelace.h>
//
//   #endif  // MAC_OS_X_VERSION_10_77
//
// ----------------------------------------------------------------------------

#if !defined(MAC_OS_X_VERSION_10_15) || \
    MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_15

@interface NSScreen (ForwardDeclare)
@property(readonly)
    CGFloat maximumPotentialExtendedDynamicRangeColorComponentValue
        API_AVAILABLE(macos(10.15));
@end

NS_CLASS_AVAILABLE_MAC(10_15)
@interface SFUniversalLink : NSObject
- (instancetype)initWithWebpageURL:(NSURL*)url;
@property(readonly) NSURL* webpageURL;
@property(readonly) NSURL* applicationURL;
@property(getter=isEnabled) BOOL enabled;
@end

#else

#import <SafariServices/SafariServices.h>

#endif  // MAC_OS_X_VERSION_10_15


#endif  // BASE_MAC_SDK_FORWARD_DECLARATIONS_H_
