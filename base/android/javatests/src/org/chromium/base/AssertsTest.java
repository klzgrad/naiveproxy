// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.support.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseJUnit4ClassRunner;

/**
 * Test that ensures Java asserts are working.
 *
 * Not a robolectric test because we want to make sure asserts are enabled after dexing.
 */
@RunWith(BaseJUnit4ClassRunner.class)
public class AssertsTest {
    @Test
    @SmallTest
    @SuppressWarnings("UseCorrectAssertInTests")
    public void testAssertsWorkAsExpected() {
        if (BuildConfig.DCHECK_IS_ON) {
            try {
                assert false;
            } catch (AssertionError e) {
                // When DCHECK is on, asserts should throw AssertionErrors.
                return;
            }
            Assert.fail("Java assert unexpectedly didn't fire.");
        } else {
            // When DCHECK isn't on, asserts should be removed by proguard.
            assert false : "Java assert unexpectedly fired.";
        }
    }
}
