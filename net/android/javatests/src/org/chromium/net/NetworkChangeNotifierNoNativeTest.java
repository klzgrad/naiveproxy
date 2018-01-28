// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.os.Looper;
import android.support.test.filters.MediumTest;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseJUnit4ClassRunner;

/**
 * Tests for org.chromium.net.NetworkChangeNotifier without native code. This class specifically
 * does not have a setUp() method that loads native libraries.
 */
@RunWith(BaseJUnit4ClassRunner.class)
public class NetworkChangeNotifierNoNativeTest {
    /**
     * Verify NetworkChangeNotifier can initialize without calling into native code. This test
     * will crash if any native calls are made during NetworkChangeNotifier initialization.
     */
    @Test
    @MediumTest
    public void testNoNativeDependence() {
        Looper.prepare();
        NetworkChangeNotifier.init();
        NetworkChangeNotifier.registerToReceiveNotificationsAlways();
    }
}