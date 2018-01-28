// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import android.os.Bundle;

import org.chromium.base.process_launcher.ICallbackInt;

interface IChildProcessService {
  // On the first call to this method, the service will record the calling PID
  // and return true. Subsequent calls will only return true if the calling PID
  // is the same as the recorded one.
  boolean bindToCaller();

  // Sets up the initial IPC channel.
  oneway void setupConnection(in Bundle args, ICallbackInt pidCallback,
          in List<IBinder>  clientInterfaces);

  // Asks the child service to crash so that we can test the termination logic.
  oneway void crashIntentionallyForTesting();
}
