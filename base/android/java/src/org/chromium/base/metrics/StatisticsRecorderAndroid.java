// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.metrics;

import org.chromium.base.annotations.JNINamespace;

/**
 * Java API which exposes the registered histograms on the native side as
 * JSON test.
 */
@JNINamespace("base::android")
public final class StatisticsRecorderAndroid {
    private StatisticsRecorderAndroid() {}

    /**
      * @return All the registered histograms as JSON text.
      */
    public static String toJson() {
        return nativeToJson();
    }

    private static native String nativeToJson();
}