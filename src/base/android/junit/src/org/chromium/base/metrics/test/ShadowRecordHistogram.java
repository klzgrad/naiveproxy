// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.metrics.test;

import android.util.Pair;

import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.annotation.Resetter;

import org.chromium.base.metrics.CachedMetrics;
import org.chromium.base.metrics.RecordHistogram;

import java.util.HashMap;
import java.util.Map;

/**
 * Implementation of RecordHistogram which does not rely on native and still enables testing of
 * histogram counts.
 */
@Implements(RecordHistogram.class)
public class ShadowRecordHistogram {
    private static final Map<Pair<String, Integer>, Integer> sSamples = new HashMap<>();
    private static final Map<String, Integer> sTotals = new HashMap<>();

    @Resetter
    public static void reset() {
        sSamples.clear();
        sTotals.clear();
    }

    @Implementation
    public static void recordBooleanHistogram(String name, boolean sample) {
        Pair<String, Integer> key = Pair.create(name, sample ? 1 : 0);
        recordSample(key);
    }

    @Implementation
    public static void recordCountHistogram(String name, int sample) {
        Pair<String, Integer> key = Pair.create(name, sample);
        recordSample(key);
    }

    @Implementation
    public static void recordCount100Histogram(String name, int sample) {
        Pair<String, Integer> key = Pair.create(name, sample);
        recordSample(key);
    }

    @Implementation
    public static void recordCustomCountHistogram(
            String name, int sample, int min, int max, int numBuckets) {
        Pair<String, Integer> key = Pair.create(name, sample);
        recordSample(key);
    }

    @Implementation
    public static void recordEnumeratedHistogram(String name, int sample, int boundary) {
        assert sample < boundary : "Sample " + sample + " is not within boundary " + boundary + "!";
        recordSample(Pair.create(name, sample));
    }

    @Implementation
    public static void recordLongTimesHistogram100(String name, long durationMs) {
        Pair<String, Integer> key = Pair.create(name, (int) durationMs);
        recordSample(key);
    }

    @Implementation
    public static int getHistogramValueCountForTesting(String name, int sample) {
        CachedMetrics.commitCachedMetrics();
        Integer i = sSamples.get(Pair.create(name, sample));
        return (i != null) ? i : 0;
    }

    @Implementation
    public static int getHistogramTotalCountForTesting(String name) {
        CachedMetrics.commitCachedMetrics();
        Integer i = sTotals.get(name);
        return (i != null) ? i : 0;
    }

    private static void recordSample(Pair<String, Integer> key) {
        Integer bucketValue = sSamples.get(key);
        if (bucketValue == null) {
            bucketValue = 0;
        }
        sSamples.put(key, bucketValue + 1);

        Integer totalCount = sTotals.get(key.first);
        if (totalCount == null) {
            totalCount = 0;
        }
        sTotals.put(key.first, totalCount + 1);
    }
}
