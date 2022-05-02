// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#import "cronet_consumer_app_delegate.h"

int main(int argc, char* argv[]) {
  @autoreleasepool {
    return UIApplicationMain(
        argc, argv, nil, NSStringFromClass([CronetConsumerAppDelegate class]));
  }
}
