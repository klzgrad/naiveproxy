// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/enterprise_util.h"

#import <OpenDirectory/OpenDirectory.h>

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/mac/sdk_forward_declarations.h"

namespace base {

bool IsMachineExternallyManaged() {
  @autoreleasepool {
    ODSession* session = [ODSession defaultSession];
    if (session == nil) {
      DLOG(WARNING) << "ODSession default session is nil.";
      return false;
    }

    NSError* error = nil;

    NSArray<NSString*>* all_node_names =
        [session nodeNamesAndReturnError:&error];
    if (!all_node_names) {
      DLOG(WARNING) << "ODSession failed to give node names: "
                    << error.localizedDescription.UTF8String;
      return false;
    }

    NSUInteger num_nodes = all_node_names.count;
    if (num_nodes < 3) {
      DLOG(WARNING) << "ODSession returned too few node names: "
                    << all_node_names.description.UTF8String;
      return false;
    }

    if (num_nodes > 3) {
      // Non-enterprise machines have:"/Search", "/Search/Contacts",
      // "/Local/Default". Everything else would be enterprise management.
      return true;
    }

    ODNode* node = [ODNode nodeWithSession:session
                                      type:kODNodeTypeAuthentication
                                     error:&error];
    if (node == nil) {
      DLOG(WARNING) << "ODSession cannot obtain the authentication node: "
                    << error.localizedDescription.UTF8String;
      return false;
    }

    // Now check the currently logged on user.
    ODQuery* query = [ODQuery queryWithNode:node
                             forRecordTypes:kODRecordTypeUsers
                                  attribute:kODAttributeTypeRecordName
                                  matchType:kODMatchEqualTo
                                queryValues:NSUserName()
                           returnAttributes:kODAttributeTypeAllAttributes
                             maximumResults:0
                                      error:&error];
    if (query == nil) {
      DLOG(WARNING) << "ODSession cannot create user query: "
                    << base::mac::NSToCFCast(error);
      return false;
    }

    NSArray* results = [query resultsAllowingPartial:NO error:&error];
    if (!results) {
      DLOG(WARNING) << "ODSession cannot obtain current user node: "
                    << error.localizedDescription.UTF8String;
      return false;
    }
    if (results.count != 1) {
      DLOG(WARNING) << @"ODSession unexpected number of user nodes: "
                    << results.count;
    }
    for (id element in results) {
      ODRecord* record = base::mac::ObjCCastStrict<ODRecord>(element);
      NSArray* attributes =
          [record valuesForAttribute:kODAttributeTypeMetaRecordName error:nil];
      for (id attribute in attributes) {
        NSString* attribute_value =
            base::mac::ObjCCastStrict<NSString>(attribute);
        // Example: "uid=johnsmith,ou=People,dc=chromium,dc=org
        NSRange domain_controller =
            [attribute_value rangeOfString:@"(^|,)\\s*dc="
                                   options:NSRegularExpressionSearch];
        if (domain_controller.length > 0) {
          return true;
        }
      }

      // Scan alternative identities.
      attributes =
          [record valuesForAttribute:kODAttributeTypeAltSecurityIdentities
                               error:nil];
      for (id attribute in attributes) {
        NSString* attribute_value =
            base::mac::ObjCCastStrict<NSString>(attribute);
        NSRange icloud =
            [attribute_value rangeOfString:@"CN=com.apple.idms.appleid.prd"
                                   options:NSCaseInsensitiveSearch];
        if (!icloud.length) {
          // Any alternative identity that is not iCloud is likely enterprise
          // management.
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace base
