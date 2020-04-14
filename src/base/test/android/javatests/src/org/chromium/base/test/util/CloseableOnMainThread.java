// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import android.support.test.InstrumentationRegistry;

import java.io.Closeable;
import java.io.IOException;
import java.util.concurrent.Callable;

/**
 * A Closeable that manages another Closeable running on android's main thread.
 *
 * The Closeable is both created and closed on the main thread.
 * Note that both operations are synchronous.
 */
public final class CloseableOnMainThread implements Closeable {
    private Closeable mCloseable;
    private Exception mException;

    /**
     * Execute a closeable callable on the main thread, blocking until it is complete.
     *
     * @param initializer A closeable callable to be executed on the main thread
     * @throws Exception Thrown if the initializer throws Exception
     */
    public CloseableOnMainThread(Callable<Closeable> initializer) throws Exception {
        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            try {
                mCloseable = initializer.call();
            } catch (Exception e) {
                mException = e;
            }
        });
        if (mException != null) {
            throw new Exception(mException.getCause());
        }
    }

    /**
     * Close the closeable on the main thread, blocking until it is complete.
     *
     * @throws IOException Thrown if the closeable throws IOException
     */
    @Override
    public void close() throws IOException {
        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            try {
                mCloseable.close();
            } catch (IOException e) {
                mException = e;
            }
        });
        if (mException != null) {
            throw new IOException(mException.getCause());
        }
    }
}
