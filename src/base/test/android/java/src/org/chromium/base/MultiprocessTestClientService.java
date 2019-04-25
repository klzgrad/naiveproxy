// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.chromium.base.process_launcher.ChildProcessService;

/** The service implementation used to host all multiprocess test client code. */
public class MultiprocessTestClientService extends ChildProcessService {
    public MultiprocessTestClientService() {
        super(new MultiprocessTestClientServiceDelegate());
    }
}
