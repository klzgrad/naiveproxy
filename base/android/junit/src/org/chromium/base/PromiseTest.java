// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.Promise.UnhandledRejectionException;
import org.chromium.testing.local.LocalRobolectricTestRunner;

/** Unit tests for {@link Promise}. */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class PromiseTest {
    // We need a simple mutable reference type for testing.
    private static class Value {
        private int mValue;

        public int get() {
            return mValue;
        }

        public void set(int value) {
            mValue = value;
        }
    }

    /** Tests that the callback is called on fulfillment. */
    @Test
    public void callback() {
        final Value value = new Value();

        Promise<Integer> promise = new Promise<Integer>();
        promise.then(PromiseTest.<Integer>setValue(value, 1));

        assertEquals(value.get(), 0);

        promise.fulfill(new Integer(1));
        assertEquals(value.get(), 1);
    }

    /** Tests that multiple callbacks are called. */
    @Test
    public void multipleCallbacks() {
        final Value value = new Value();

        Promise<Integer> promise = new Promise<Integer>();
        Callback<Integer> callback = new Callback<Integer>() {
            @Override
            public void onResult(Integer result) {
                value.set(value.get() + 1);
            }
        };
        promise.then(callback);
        promise.then(callback);

        assertEquals(value.get(), 0);

        promise.fulfill(new Integer(0));
        assertEquals(value.get(), 2);
    }

    /** Tests that a callback is called immediately when given to a fulfilled Promise. */
    @Test
    public void callbackOnFulfilled() {
        final Value value = new Value();

        Promise<Integer> promise = Promise.fulfilled(new Integer(0));
        assertEquals(value.get(), 0);

        promise.then(PromiseTest.<Integer>setValue(value, 1));

        assertEquals(value.get(), 1);
    }

    /** Tests that promises can chain synchronous functions correctly. */
    @Test
    public void promiseChaining() {
        Promise<Integer> promise = new Promise<Integer>();
        final Value value = new Value();

        promise.then(new Promise.Function<Integer, String>(){
                    @Override
                    public String apply(Integer arg) {
                        return arg.toString();
                    }
                }).then(new Promise.Function<String, String>(){
                    @Override
                    public String apply(String arg) {
                        return arg + arg;
                    }
                }).then(new Callback<String>() {
                    @Override
                    public void onResult(String result) {
                        value.set(result.length());
                    }
                });

        promise.fulfill(new Integer(123));
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(6, value.get());
    }

    /** Tests that promises can chain asynchronous functions correctly. */
    @Test
    public void promiseChainingAsyncFunctions() {
        Promise<Integer> promise = new Promise<Integer>();
        final Value value = new Value();

        final Promise<String> innerPromise = new Promise<String>();

        promise.then(new Promise.AsyncFunction<Integer, String>() {
                    @Override
                    public Promise<String> apply(Integer arg) {
                        return innerPromise;
                    }
                }).then(new Callback<String>(){
                    @Override
                    public void onResult(String result) {
                        value.set(result.length());
                    }
                });

        assertEquals(0, value.get());

        promise.fulfill(5);
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(0, value.get());

        innerPromise.fulfill("abc");
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(3, value.get());
    }

    /** Tests that a Promise that does not use its result does not throw on rejection. */
    @Test
    public void rejectPromiseNoCallbacks() {
        Promise<Integer> promise = new Promise<Integer>();

        boolean caught = false;
        try {
            promise.reject();
            ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        } catch (UnhandledRejectionException e) {
            caught = true;
        }
        assertFalse(caught);
    }

    /** Tests that a Promise that uses its result throws on rejection if it has no handler. */
    @Test
    public void rejectPromiseNoHandler() {
        Promise<Integer> promise = new Promise<Integer>();
        promise.then(PromiseTest.<Integer>identity()).then(PromiseTest.<Integer>pass());

        boolean caught = false;
        try {
            promise.reject();
            ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        } catch (UnhandledRejectionException e) {
            caught = true;
        }
        assertTrue(caught);
    }

    /** Tests that a Promise that handles rejection does not throw on rejection. */
    @Test
    public void rejectPromiseHandled() {
        Promise<Integer> promise = new Promise<Integer>();
        promise.then(PromiseTest.<Integer>identity())
                .then(PromiseTest.<Integer>pass(), PromiseTest.<Exception>pass());

        boolean caught = false;
        try {
            promise.reject();
            ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        } catch (UnhandledRejectionException e) {
            caught = true;
        }
        assertFalse(caught);
    }

    /** Tests that rejections carry the exception information. */
    @Test
    public void rejectionInformation() {
        Promise<Integer> promise = new Promise<Integer>();
        promise.then(PromiseTest.<Integer>pass());

        String message = "Promise Test";
        try {
            promise.reject(new NegativeArraySizeException(message));
            ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        } catch (UnhandledRejectionException e) {
            assertTrue(e.getCause() instanceof NegativeArraySizeException);
            assertEquals(e.getCause().getMessage(), message);
        }
    }

    /** Tests that rejections propagate. */
    @Test
    public void rejectionChaining() {
        final Value value = new Value();
        Promise<Integer> promise = new Promise<Integer>();

        Promise<Integer> result =
                promise.then(PromiseTest.<Integer>identity()).then(PromiseTest.<Integer>identity());

        result.then(PromiseTest.<Integer>pass(), PromiseTest.<Exception>setValue(value, 5));

        promise.reject(new Exception());
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();

        assertEquals(value.get(), 5);
        assertTrue(result.isRejected());
    }

    /** Tests that Promises get rejected if a Function throws. */
    @Test
    public void rejectOnThrow() {
        Value value = new Value();
        Promise<Integer> promise = new Promise<Integer>();
        promise.then(new Promise.Function<Integer, Integer>() {
            @Override
            public Integer apply(Integer argument) {
                throw new IllegalArgumentException();
            }
        }).then(PromiseTest.<Integer>pass(), PromiseTest.<Exception>setValue(value, 5));

        promise.fulfill(0);

        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(value.get(), 5);
    }

    /** Tests that Promises get rejected if an AsyncFunction throws. */
    @Test
    public void rejectOnAsyncThrow() {
        Value value = new Value();
        Promise<Integer> promise = new Promise<Integer>();

        promise.then(new Promise.AsyncFunction<Integer, Integer>() {
            @Override
            public Promise<Integer> apply(Integer argument) {
                throw new IllegalArgumentException();
            }
        }).then(PromiseTest.<Integer>pass(), PromiseTest.<Exception>setValue(value, 5));

        promise.fulfill(0);

        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(value.get(), 5);
    }

    /** Tests that Promises get rejected if an AsyncFunction rejects. */
    @Test
    public void rejectOnAsyncReject() {
        Value value = new Value();
        Promise<Integer> promise = new Promise<Integer>();
        final Promise<Integer> inner = new Promise<Integer>();

        promise.then(new Promise.AsyncFunction<Integer, Integer>() {
            @Override
            public Promise<Integer> apply(Integer argument) {
                return inner;
            }
        }).then(PromiseTest.<Integer>pass(), PromiseTest.<Exception>setValue(value, 5));

        promise.fulfill(0);

        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(value.get(), 0);

        inner.reject();

        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(value.get(), 5);
    }

    /** Convenience method that returns a Callback that does nothing with its result. */
    private static <T> Callback<T> pass() {
        return new Callback<T>() {
            @Override
            public void onResult(T result) {}
        };
    }

    /** Convenience method that returns a Function that just passes through its argument. */
    private static <T> Promise.Function<T, T> identity() {
        return new Promise.Function<T, T>() {
            @Override
            public T apply(T argument) {
                return argument;
            }
        };
    }

    /** Convenience method that returns a Callback that sets the given Value on execution. */
    private static <T> Callback<T> setValue(final Value toSet, final int value) {
        return new Callback<T>() {
            @Override
            public void onResult(T result) {
                toSet.set(value);
            }
        };
    }
}