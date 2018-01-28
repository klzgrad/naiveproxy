// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.annotations.SuppressFBWarnings;

@MainDex
@SuppressFBWarnings("NM_CLASS_NOT_EXCEPTION")
abstract class ThrowUncaughtException {
    @CalledByNative
    private static void post() {
        ThreadUtils.postOnUiThread(new Runnable() {
            @Override
            public void run() {
                throw new RuntimeException("Intentional exception not caught by JNI");
            }
        });
    }
}
