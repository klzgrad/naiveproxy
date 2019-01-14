// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net.test;

import android.support.test.InstrumentationRegistry;

import org.junit.rules.TestWatcher;
import org.junit.runner.Description;

/**
 * Junit4 rule for starting embedded test server before a test starts, and shutting it down when it
 * finishes.
 */
public class EmbeddedTestServerRule extends TestWatcher {
    EmbeddedTestServer mServer;

    @Override
    protected void starting(Description description) {
        try {
            mServer = EmbeddedTestServer.createAndStartServer(InstrumentationRegistry.getContext());
        } catch (InterruptedException e) {
            throw new EmbeddedTestServer.EmbeddedTestServerFailure("Test server didn't start");
        }
        super.starting(description);
    }

    @Override
    protected void finished(Description description) {
        super.finished(description);
        mServer.stopAndDestroyServer();
    }

    /**
     * Get the test server.
     *
     * @return the test server.
     */
    public EmbeddedTestServer getServer() {
        return mServer;
    }

    public String getOrigin() {
        return mServer.getURL("/");
    }
}
