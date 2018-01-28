// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;

/** Time-related utilities. */
@JNINamespace("base::android")
@MainDex
public class TimeUtils {
    private TimeUtils() {}

    /** Returns TimeTicks::Now() in microseconds. */
    public static native long nativeGetTimeTicksNowUs();
}
