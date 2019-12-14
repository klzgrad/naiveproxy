// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

interface IParentProcess {
    // Sends the child pid to the parent process. This will be called before any
    // third-party code is loaded, and will be a no-op after the first call.
    oneway void sendPid(int pid);

    // Tells the parent proces the child exited cleanly. Not oneway to ensure
    // the browser receives the message before child exits.
    void reportCleanExit();
}
