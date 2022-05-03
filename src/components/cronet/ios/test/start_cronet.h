// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

namespace cronet {

// Starts Cronet, or restarts if Cronet is already running.  Will have Cronet
// point test.example.com" to "localhost:|port|".
void StartCronet(int port);

}  // namespace cronet
