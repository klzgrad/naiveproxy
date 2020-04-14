// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.metrics;

import android.support.annotation.Nullable;
import android.support.annotation.VisibleForTesting;

import androidx.annotation.IntDef;

import org.chromium.base.Log;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.locks.ReentrantReadWriteLock;

import javax.annotation.concurrent.GuardedBy;

/**
 * Stores samples until given an {@link UmaRecorder} to flush the samples to. After flushing, no
 * longer stores samples, instead immediately delegates to the given {@link UmaRecorder}.
 */
/* package */ final class CachingUmaRecorder implements UmaRecorder {
    private static final String TAG = "CachingUmaRecorder";

    /**
     * Maximum number of histograms cached at the same time. It is better to drop some samples
     * rather than have a bug cause the cache to grow without limit.
     * <p>
     * Each sample uses 4 bytes, each histogram uses approx. 12 references (4 bytes each).
     * With {@code MAX_HISTOGRAM_COUNT = 256} and {@code MAX_SAMPLE_COUNT = 256} this limits cache
     * size to 270KiB. Changing either value by one, adds or removes approx. 1KiB.
     */
    private static final int MAX_HISTOGRAM_COUNT = 256;

    /** Stores the definition and samples of a single cached histogram. */
    @VisibleForTesting
    static class Histogram {
        /**
         * Maximum number of cached samples in a single histogram. it is better to drop some
         * samples rather than have a bug cause the cache to grow without limit
         */
        @VisibleForTesting
        static final int MAX_SAMPLE_COUNT = 256;

        /**
         * Identifies the type of the histogram.
         */
        @IntDef({
                Type.BOOLEAN,
                Type.EXPONENTIAL,
                Type.LINEAR,
                Type.SPARSE,
        })
        @Retention(RetentionPolicy.SOURCE)
        @interface Type {
            /**
             * Used by histograms recorded with {@link UmaRecorder#recordBooleanHistogram}.
             */
            int BOOLEAN = 1;
            /**
             * Used by histograms recorded with {@link UmaRecorder#recordExponentialHistogram}.
             */
            int EXPONENTIAL = 2;

            /**
             * Used by histograms recorded with {@link UmaRecorder#recordLinearHistogram}.
             */
            int LINEAR = 3;

            /**
             * Used by histograms recorded with {@link UmaRecorder#recordSparseHistogram}.
             */
            int SPARSE = 4;
        }

        @Type
        private final int mType;
        private final String mName;

        private final int mMin;
        private final int mMax;
        private final int mNumBuckets;

        @GuardedBy("this")
        private final List<Integer> mSamples;

        /**
         * Constructs a {@code Histogram} with the specified definition and no samples.
         *
         * @param type histogram type.
         * @param name histogram name.
         * @param min histogram min value. Must be {@code 0} for boolean or sparse histograms.
         * @param max histogram max value. Must be {@code 0} for boolean or sparse histograms.
         * @param numBuckets number of histogram buckets. Must be {@code 0} for boolean or sparse
         *         histograms.
         */
        Histogram(@Type int type, String name, int min, int max, int numBuckets) {
            assert type == Type.EXPONENTIAL || type == Type.LINEAR
                    || (min == 0 && max == 0 && numBuckets == 0)
                : "Histogram type " + type + " must have no min/max/buckets set";
            mType = type;
            mName = name;
            mMin = min;
            mMax = max;
            mNumBuckets = numBuckets;

            mSamples = new ArrayList<>(/*initialCapacity=*/1);
        }

        /**
         * Appends a sample to values cached in this histogram. Verifies that histogram definition
         * matches the definition used to create this object: attempts to fail with an assertion,
         * otherwise records failure statistics.
         *
         * @param type histogram type.
         * @param name histogram name.
         * @param sample sample value to cache.
         * @param min histogram min value. Must be {@code 0} for boolean or sparse histograms.
         * @param max histogram max value. Must be {@code 0} for boolean or sparse histograms.
         * @param numBuckets number of histogram buckets. Must be {@code 0} for boolean or sparse
         *         histograms.
         * @return true if the sample was recorded.
         */
        synchronized boolean addSample(
                @Type int type, String name, int sample, int min, int max, int numBuckets) {
            assert mType == type;
            assert mName.equals(name);
            assert mMin == min;
            assert mMax == max;
            assert mNumBuckets == numBuckets;
            if (mSamples.size() >= MAX_SAMPLE_COUNT) {
                // A cache filling up is most likely an indication of a bug.
                assert false : "Histogram exceeded sample cache size limit";
                return false;
            }
            mSamples.add(sample);
            return true;
        }

        /**
         * Writes all samples to {@code recorder}, clears cached samples.
         *
         * @param recorder destination {@link UmaRecorder}.
         * @return number of flushed samples.
         */
        synchronized int flushTo(UmaRecorder recorder) {
            switch (mType) {
                case Type.BOOLEAN:
                    for (int i = 0; i < mSamples.size(); i++) {
                        final int sample = mSamples.get(i);
                        recorder.recordBooleanHistogram(mName, sample != 0);
                    }
                    break;
                case Type.EXPONENTIAL:
                    for (int i = 0; i < mSamples.size(); i++) {
                        final int sample = mSamples.get(i);
                        recorder.recordExponentialHistogram(mName, sample, mMin, mMax, mNumBuckets);
                    }
                    break;
                case Type.LINEAR:
                    for (int i = 0; i < mSamples.size(); i++) {
                        final int sample = mSamples.get(i);
                        recorder.recordLinearHistogram(mName, sample, mMin, mMax, mNumBuckets);
                    }
                    break;
                case Type.SPARSE:
                    for (int i = 0; i < mSamples.size(); i++) {
                        final int sample = mSamples.get(i);
                        recorder.recordSparseHistogram(mName, sample);
                    }
                    break;
                default:
                    assert false : "Unknown histogram type " + mType;
            }
            int count = mSamples.size();
            mSamples.clear();
            return count;
        }
    }

    /**
     * The lock doesn't need to be fair - in the worst case a writing record*Histogram call will be
     * starved until reading calls reach cache size limits.
     * <p>
     * A read-write lock is used rather than {@code synchronized} blocks to the limit opportunities
     * for stutter on the UI thread when waiting for this shared resource.
     */
    private final ReentrantReadWriteLock mRwLock = new ReentrantReadWriteLock(/*fair=*/false);

    @GuardedBy("mRwLock")
    private Map<String, Histogram> mHistogramByName = new HashMap<>();

    /**
     * If not null, all metrics are forwarded to this {@link UmaRecorder}.
     * <p>
     * The read lock must be held while invoking methods on {@code mDelegate}.
     */
    @GuardedBy("mRwLock")
    @Nullable
    private UmaRecorder mDelegate;

    /**
     * Number of samples that couldn't be cached, because the number of histograms in cache has
     * reached its limit.
     */
    @GuardedBy("mRwLock")
    private int mHistogramLimitSampleCount;

    /**
     * Number of samples that couldn't be cached, because the number of samples for a histogram has
     * been reached.
     */
    @GuardedBy("this")
    private int mDroppedSampleCount;

    /**
     * Replaces the current delegate (if any) with {@code recorder}. Writes and clears all cached
     * samples if {@code recorder} is not null.
     *
     * @param recorder new delegate.
     * @return the previous delegate.
     */
    public UmaRecorder setDelegate(@Nullable final UmaRecorder recorder) {
        UmaRecorder previous;
        Map<String, Histogram> cache = null;
        int histogramLimitSampleCount = 0;
        int droppedSampleCount = 0;
        mRwLock.writeLock().lock();
        try {
            previous = mDelegate;
            mDelegate = recorder;
            if (recorder != null && !mHistogramByName.isEmpty()) {
                cache = mHistogramByName;
                mHistogramByName = new HashMap<>();
                histogramLimitSampleCount = mHistogramLimitSampleCount;
                mHistogramLimitSampleCount = 0;
                synchronized (this) {
                    droppedSampleCount = mDroppedSampleCount;
                    mDroppedSampleCount = 0;
                }
            }
            mRwLock.readLock().lock();
        } finally {
            mRwLock.writeLock().unlock();
        }
        // Cache is flushed only after downgrading from a write lock to a read lock.
        try {
            if (cache != null) {
                flushCacheAlreadyLocked(cache, histogramLimitSampleCount, droppedSampleCount);
            }
        } finally {
            mRwLock.readLock().unlock();
        }
        return previous;
    }

    /**
     * Writes metrics from {@code cache} to the delegate. Assumes that a read lock is held by the
     * current thread.
     *
     * @param cache the cache to be flushed.
     * @param histogramLimitSampleCount number of samples that were not recorded in {@code cache} to
     *         stay within {@link MAX_HISTOGRAM_COUNT}.
     * @param droppedSampleCount number of samples that were not recerded in {@code cache} to stay
     *         within {@link Histogram.MAX_SAMPLE_COUNT}.
     */
    @GuardedBy("mRwLock")
    private void flushCacheAlreadyLocked(
            Map<String, Histogram> cache, int histogramLimitSampleCount, int droppedSampleCount) {
        assert mDelegate != null : "Unexpected: cache is flushed, but delegate is null";
        assert mRwLock.getReadHoldCount() > 0;
        int flushedSampleCount = 0;
        final int flushedHistogramCount = cache.size();
        int fullHistogramCount = 0;
        int remainingSampleLimit = Histogram.MAX_SAMPLE_COUNT;
        for (Histogram histogram : cache.values()) {
            int flushed = histogram.flushTo(mDelegate);
            flushedSampleCount += flushed;
            if (flushed >= Histogram.MAX_SAMPLE_COUNT) {
                fullHistogramCount++;
            }
            remainingSampleLimit =
                    Math.min(remainingSampleLimit, Histogram.MAX_SAMPLE_COUNT - flushed);
        }
        Log.i(TAG, "Flushed %d samples from %d histograms.", flushedSampleCount,
                flushedHistogramCount);
        // Using RecordHistogram could cause an infinite recursion.
        mDelegate.recordExponentialHistogram("UMA.JavaCachingRecorder.FlushedHistogramCount",
                flushedHistogramCount, 1, 100_000, 50);
        mDelegate.recordExponentialHistogram(
                "UMA.JavaCachingRecorder.FullHistogramCount", fullHistogramCount, 1, 100_000, 50);
        mDelegate.recordExponentialHistogram("UMA.JavaCachingRecorder.InputSampleCount",
                flushedSampleCount + droppedSampleCount, 1, 1_000_000, 50);
        mDelegate.recordExponentialHistogram(
                "UMA.JavaCachingRecorder.DroppedSampleCount", droppedSampleCount, 1, 1_000_000, 50);
        mDelegate.recordExponentialHistogram(
                "UMA.JavaCachingRecorder.HistogramLimitDroppedSampleCount",
                histogramLimitSampleCount, 1, 1_000_000, 50);
        mDelegate.recordExponentialHistogram(
                "UMA.JavaCachingRecorder.RemainingSampleLimit", remainingSampleLimit, 1, 1_000, 50);
        mDelegate.recordExponentialHistogram("UMA.JavaCachingRecorder.RemainingHistogramLimit",
                MAX_HISTOGRAM_COUNT - flushedHistogramCount, 1, 1_000, 50);
    }

    /**
     * Delegates or stores a sample. Stores samples iff there is no delegate {@link UmaRecorder}
     * set.
     *
     * @param type histogram type.
     * @param name histogram name.
     * @param sample sample value.
     * @param min histogram min value.
     * @param max histogram max value.
     * @param numBuckets number of histogram buckets.
     */
    private void cacheOrRecordSample(
            @Histogram.Type int type, String name, int sample, int min, int max, int numBuckets) {
        // Optimistic attempt without creating a Histogram.
        if (tryAppendOrRecordSample(type, name, sample, min, max, numBuckets)) {
            return;
        }

        mRwLock.writeLock().lock();
        try {
            if (mDelegate == null) {
                appendSampleMaybeCreateHistogramAlreadyLocked(
                        type, name, sample, min, max, numBuckets);
                return; // Skip the lock downgrade.
            }
            mRwLock.readLock().lock();
        } finally {
            mRwLock.writeLock().unlock();
        }
        // Downgraded to read lock.
        try {
            recordSampleAlreadyLocked(type, name, sample, min, max, numBuckets);
        } finally {
            mRwLock.readLock().unlock();
        }
    }

    /**
     * Tries to cache or record a sample without creating a new {@link Histogram}.
     *
     * @param type histogram type.
     * @param name histogram name.
     * @param sample sample value.
     * @param min histogram min value.
     * @param max histogram max value.
     * @param numBuckets number of histogram buckets.
     * @return {@code false} if the sample needs to be recorded with a write lock.
     */
    private boolean tryAppendOrRecordSample(
            @Histogram.Type int type, String name, int sample, int min, int max, int numBuckets) {
        mRwLock.readLock().lock();
        try {
            if (mDelegate != null) {
                recordSampleAlreadyLocked(type, name, sample, min, max, numBuckets);
                return true;
            }
            Histogram histogram = mHistogramByName.get(name);
            if (histogram == null) {
                return false;
            }
            if (!histogram.addSample(type, name, sample, min, max, numBuckets)) {
                synchronized (this) {
                    mDroppedSampleCount++;
                }
            }
            return true;
        } finally {
            mRwLock.readLock().unlock();
        }
    }

    /**
     * Appends a {@code sample} to a cached {@link Histogram}. Creates the {@code Histogram}
     * if needed. Assumes that the <b>write lock</b> is held by the current thread.
     *
     * @param type histogram type.
     * @param name histogram name.
     * @param sample sample value.
     * @param min histogram min value.
     * @param max histogram max value.
     * @param numBuckets number of histogram buckets.
     */
    @GuardedBy("mRwLock")
    private void appendSampleMaybeCreateHistogramAlreadyLocked(
            @Histogram.Type int type, String name, int sample, int min, int max, int numBuckets) {
        assert mRwLock.isWriteLockedByCurrentThread();
        Histogram histogram = mHistogramByName.get(name);
        if (histogram == null) {
            if (mHistogramByName.size() >= MAX_HISTOGRAM_COUNT) {
                // A cache filling up is most likely an indication of a bug.
                assert false : "Too many histograms in cache";
                mHistogramLimitSampleCount++;
                return;
            }
            histogram = new Histogram(type, name, min, max, numBuckets);
            mHistogramByName.put(name, histogram);
        }
        if (!histogram.addSample(type, name, sample, min, max, numBuckets)) {
            synchronized (this) {
                mDroppedSampleCount++;
            }
        }
    }

    /**
     * Records a sample with delegate. Assumes that a read lock is held by the current thread.
     * Shouldn't be called with a write lock held.
     *
     * @param type histogram type.
     * @param name histogram name.
     * @param sample sample value.
     * @param min histogram min value.
     * @param max histogram max value.
     * @param numBuckets number of histogram buckets.
     */
    @GuardedBy("mRwLock")
    private void recordSampleAlreadyLocked(
            @Histogram.Type int type, String name, int sample, int min, int max, int numBuckets) {
        assert mRwLock.getReadHoldCount() > 0;
        assert !mRwLock.isWriteLockedByCurrentThread();
        assert mDelegate != null : "recordSampleAlreadyLocked called with no delegate to record to";
        switch (type) {
            case Histogram.Type.BOOLEAN:
                mDelegate.recordBooleanHistogram(name, sample != 0);
                break;
            case Histogram.Type.EXPONENTIAL:
                mDelegate.recordExponentialHistogram(name, sample, min, max, numBuckets);
                break;
            case Histogram.Type.LINEAR:
                mDelegate.recordLinearHistogram(name, sample, min, max, numBuckets);
                break;
            case Histogram.Type.SPARSE:
                mDelegate.recordSparseHistogram(name, sample);
                break;
            default:
                throw new UnsupportedOperationException("Unknown histogram type " + type);
        }
    }

    @Override
    public void recordBooleanHistogram(String name, boolean boolSample) {
        final int sample = boolSample ? 1 : 0;
        final int min = 0;
        final int max = 0;
        final int numBuckets = 0;
        cacheOrRecordSample(Histogram.Type.BOOLEAN, name, sample, min, max, numBuckets);
    }

    @Override
    public void recordExponentialHistogram(
            String name, int sample, int min, int max, int numBuckets) {
        cacheOrRecordSample(Histogram.Type.EXPONENTIAL, name, sample, min, max, numBuckets);
    }

    @Override
    public void recordLinearHistogram(String name, int sample, int min, int max, int numBuckets) {
        cacheOrRecordSample(Histogram.Type.LINEAR, name, sample, min, max, numBuckets);
    }

    @Override
    public void recordSparseHistogram(String name, int sample) {
        final int min = 0;
        final int max = 0;
        final int numBuckets = 0;
        cacheOrRecordSample(Histogram.Type.SPARSE, name, sample, min, max, numBuckets);
    }
}
