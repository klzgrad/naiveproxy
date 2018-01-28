// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.metrics.test;

import android.util.Pair;

import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.annotation.Resetter;

import org.chromium.base.metrics.RecordHistogram;

import java.util.HashMap;

/**
 * Implementation of RecordHistogram which does not rely on native and still enables testing of
 * histogram counts.
 */
@Implements(RecordHistogram.class)
public class ShadowRecordHistogram {
    private static HashMap<Pair<String, Integer>, Integer> sSamples =
            new HashMap<Pair<String, Integer>, Integer>();

    @Resetter
    public static void reset() {
        sSamples.clear();
    }

    @Implementation
    public static void recordCountHistogram(String name, int sample) {
        Pair<String, Integer> key = Pair.create(name, sample);
        Integer i = sSamples.get(key);
        if (i == null) {
            i = 0;
        }
        sSamples.put(key, i + 1);
    }

    @Implementation
    public static int getHistogramValueCountForTesting(String name, int sample) {
        Integer i = sSamples.get(Pair.create(name, sample));
        return (i != null) ? i : 0;
    }
}
