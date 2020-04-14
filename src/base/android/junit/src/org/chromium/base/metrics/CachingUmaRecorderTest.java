// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.metrics;

import static org.hamcrest.Matchers.greaterThan;
import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.verifyZeroInteractions;

import android.support.test.filters.MediumTest;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.chromium.base.test.BaseRobolectricTestRunner;

import java.time.Duration;
import java.time.Instant;
import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

/** Unit tests for {@link CachingUmaRecorderTest}. */
@RunWith(BaseRobolectricTestRunner.class)
public final class CachingUmaRecorderTest {
    @Mock
    UmaRecorder mUmaRecorder;

    @Before
    public void initMocks() {
        MockitoAnnotations.initMocks(this);
    }

    @Test
    public void testSetDelegateWithEmptyCache() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();

        cachingUmaRecorder.setDelegate(mUmaRecorder);

        verifyZeroInteractions(mUmaRecorder);
    }

    @Test
    public void testRecordBooleanHistogramGetsFlushed() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();

        cachingUmaRecorder.recordBooleanHistogram(
                "cachingUmaRecorderTest.recordBooleanHistogram", true);
        cachingUmaRecorder.recordBooleanHistogram(
                "cachingUmaRecorderTest.recordBooleanHistogram", true);
        cachingUmaRecorder.recordBooleanHistogram(
                "cachingUmaRecorderTest.recordBooleanHistogram", false);
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        verify(mUmaRecorder, times(2))
                .recordBooleanHistogram("cachingUmaRecorderTest.recordBooleanHistogram", true);
        verify(mUmaRecorder)
                .recordBooleanHistogram("cachingUmaRecorderTest.recordBooleanHistogram", false);
    }

    @Test
    public void testRecordExponentialHistogramGetsFlushed() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();

        cachingUmaRecorder.recordExponentialHistogram(
                "cachingUmaRecorderTest.recordExponentialHistogram", 72, 1, 1000, 50);
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        verify(mUmaRecorder)
                .recordExponentialHistogram(
                        "cachingUmaRecorderTest.recordExponentialHistogram", 72, 1, 1000, 50);
    }

    @Test
    public void testRecordLinearHistogramGetsFlushed() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();

        cachingUmaRecorder.recordLinearHistogram(
                "cachingUmaRecorderTest.recordLinearHistogram", 72, 1, 1000, 50);
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        verify(mUmaRecorder)
                .recordLinearHistogram(
                        "cachingUmaRecorderTest.recordLinearHistogram", 72, 1, 1000, 50);
    }

    @Test
    public void testRecordSparseHistogramGetsFlushed() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();

        cachingUmaRecorder.recordSparseHistogram(
                "cachingUmaRecorderTest.recordSparseHistogram", 72);
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        verify(mUmaRecorder)
                .recordSparseHistogram("cachingUmaRecorderTest.recordSparseHistogram", 72);
    }

    @Test
    public void testRecordBooleanHistogramDelegated() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        cachingUmaRecorder.recordBooleanHistogram(
                "CachingUmaRecorderTest.recordBooleanHistogram", true);

        verify(mUmaRecorder)
                .recordBooleanHistogram("CachingUmaRecorderTest.recordBooleanHistogram", true);
        verifyNoMoreInteractions(mUmaRecorder);
    }

    @Test
    public void testRecordExponentialHistogramDelegated() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        cachingUmaRecorder.recordExponentialHistogram(
                "CachingUmaRecorderTest.recordExponentialHistogram", 1, 2, 10, 5);

        verify(mUmaRecorder)
                .recordExponentialHistogram(
                        "CachingUmaRecorderTest.recordExponentialHistogram", 1, 2, 10, 5);
        verifyNoMoreInteractions(mUmaRecorder);
    }

    @Test
    public void testRecordLinearHistogramDelegated() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        cachingUmaRecorder.recordLinearHistogram(
                "CachingUmaRecorderTest.recordLinearHistogram", 1, 2, 10, 5);

        verify(mUmaRecorder)
                .recordLinearHistogram("CachingUmaRecorderTest.recordLinearHistogram", 1, 2, 10, 5);
        verifyNoMoreInteractions(mUmaRecorder);
    }

    @Test
    public void testRecordSparseHistogramDelegated() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        cachingUmaRecorder.recordSparseHistogram(
                "CachingUmaRecorderTest.recordSparseHistogram", 72);

        verify(mUmaRecorder)
                .recordSparseHistogram("CachingUmaRecorderTest.recordSparseHistogram", 72);
        verifyNoMoreInteractions(mUmaRecorder);
    }

    @Test
    public void testSetDelegateStopsOldDelegation() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();
        cachingUmaRecorder.setDelegate(mUmaRecorder);

        cachingUmaRecorder.setDelegate(new NoopUmaRecorder());
        cachingUmaRecorder.recordSparseHistogram("CachingUmaRecorderTest.stopOldDelegation", 72);

        verifyZeroInteractions(mUmaRecorder);
    }

    @Test
    public void testSetDelegateStartsNewDelegation() {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();
        cachingUmaRecorder.setDelegate(new NoopUmaRecorder());

        cachingUmaRecorder.setDelegate(mUmaRecorder);
        cachingUmaRecorder.recordSparseHistogram("CachingUmaRecorderTest.startNewDelegation", 72);

        verify(mUmaRecorder).recordSparseHistogram("CachingUmaRecorderTest.startNewDelegation", 72);
        verifyNoMoreInteractions(mUmaRecorder);
    }

    /**
     * {@link UmaRecorder} that has a public lock which can be used to block {@link
     * #recordSparseHistogram(String, int)} }.
     */
    private static class BlockingUmaRecorder extends NoopUmaRecorder {
        public Lock lock = new ReentrantLock();

        @SuppressWarnings("LockNotBeforeTry")
        @Override
        public void recordSparseHistogram(String name, int sample) {
            lock.lock();
            lock.unlock();
        }
    }

    @Test
    public void testSetDelegateBlocksUntilRecordingDone() throws Exception {
        CachingUmaRecorder cachingUmaRecorder = new CachingUmaRecorder();
        BlockingUmaRecorder blockingUmaRecorder = new BlockingUmaRecorder();
        cachingUmaRecorder.setDelegate(blockingUmaRecorder);

        Thread swappingThread;
        Thread recordingThread;

        blockingUmaRecorder.lock.lock();
        try {
            recordingThread = new Thread(() -> {
                cachingUmaRecorder.recordSparseHistogram(
                        "CachingUmaRecorderTest.blockUntilRecordingDone", 16);
            });
            recordingThread.start();
            awaitThreadBlocked(recordingThread, Duration.ofSeconds(1));

            swappingThread = new Thread(() -> { cachingUmaRecorder.setDelegate(mUmaRecorder); });
            swappingThread.start();
            awaitThreadBlocked(swappingThread, Duration.ofSeconds(1));
        } finally {
            blockingUmaRecorder.lock.unlock();
        }

        recordingThread.join();
        swappingThread.join();
        verifyZeroInteractions(mUmaRecorder);
    }

    @SuppressWarnings("ThreadPriorityCheck")
    private static void awaitThreadBlocked(Thread thread, Duration timeLimit) {
        final Instant start = Instant.now();

        while (true) {
            assertThat(timeLimit, greaterThan(Duration.between(start, Instant.now())));
            switch (thread.getState()) {
                case BLOCKED:
                case WAITING:
                case TIMED_WAITING:
                    return;
                case NEW:
                case RUNNABLE: {
                    Thread.yield();
                    continue;
                }
                case TERMINATED:
                    fail("Thread unexpectedly terminated.");
            }
        }
    }

    private static class TestingUmaRecorder implements UmaRecorder {
        public final AtomicIntegerArray recordedSamples;

        TestingUmaRecorder(int sampleRange) {
            recordedSamples = new AtomicIntegerArray(sampleRange);
        }

        @Override
        public void recordBooleanHistogram(String name, boolean sample) {
            throw new UnsupportedOperationException();
        }

        @Override
        public void recordExponentialHistogram(
                String name, int sample, int min, int max, int numBuckets) {
            // Ignore internal cache metrics.
            if (name.startsWith("UMA.JavaCachingRecorder")) return;
            throw new UnsupportedOperationException();
        }

        @Override
        public void recordLinearHistogram(
                String name, int sample, int min, int max, int numBuckets) {
            throw new UnsupportedOperationException();
        }

        @Override
        public void recordSparseHistogram(String name, int sample) {
            recordedSamples.addAndGet(sample, 1);
        }
    }

    @Test
    @MediumTest
    @SuppressWarnings("ThreadPriorityCheck")
    public void testStressParallel() throws Exception {
        final int numThreads = 16;
        final int numRecorders = 16;
        final int numSamples = CachingUmaRecorder.Histogram.MAX_SAMPLE_COUNT / numThreads;
        CachingUmaRecorder cachingRecorder = new CachingUmaRecorder();

        TestingUmaRecorder[] testingRecorders = new TestingUmaRecorder[numRecorders];
        for (int i = 0; i < numRecorders; i++) {
            testingRecorders[i] = new TestingUmaRecorder(numThreads);
        }
        Thread[] threads = new Thread[numThreads];
        // Each thread records a different sample value.
        for (int i = 0; i < numThreads; i++) {
            threads[i] = startRecordingThread(i, numSamples, cachingRecorder);
        }
        // Change recorders while threads are recording metrics.
        for (TestingUmaRecorder recorder : testingRecorders) {
            cachingRecorder.setDelegate(recorder);
            Thread.yield();
        }
        for (Thread thread : threads) {
            thread.join();
        }
        cachingRecorder.setDelegate(mUmaRecorder);
        verifyZeroInteractions(mUmaRecorder);
        for (int i = 0; i < numThreads; i++) {
            int actualSamples = 0;
            for (TestingUmaRecorder recorder : testingRecorders) {
                actualSamples += recorder.recordedSamples.get(i);
            }
            assertThat(String.format("thread[%d] total samples", i), actualSamples, is(numSamples));
        }
    }

    @SuppressWarnings("ThreadPriorityCheck")
    private static Thread startRecordingThread(int sample, int count, UmaRecorder recorder) {
        Thread thread = new Thread(() -> {
            for (int i = 0; i < count; i++) {
                recorder.recordSparseHistogram("StressTest", sample);
                // Make it more likely this thread will be preempted.
                Thread.yield();
            }
        });
        thread.start();
        return thread;
    }
}
