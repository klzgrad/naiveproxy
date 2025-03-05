// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.jni_zero;

class TinySample {
    @NativeMethods()
    interface Natives {
        void foo(Object a, int b);

        boolean bar(int a, Object b);
    }
}
