// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.DiscardableReferencePool.DiscardableReference;
import org.chromium.base.test.util.RetryOnFailure;
import org.chromium.testing.local.LocalRobolectricTestRunner;

import java.lang.ref.WeakReference;

/**
 * Tests for {@link DiscardableReferencePool}.
 */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class DiscardableReferencePoolTest {
    /**
     * Tests that draining the pool clears references and allows objects to be garbage collected.
     */
    @Test
    public void testDrain() {
        DiscardableReferencePool pool = new DiscardableReferencePool();

        Object object = new Object();
        WeakReference<Object> weakReference = new WeakReference<>(object);

        DiscardableReference<Object> discardableReference = pool.put(object);
        Assert.assertEquals(object, discardableReference.get());

        // Drop reference to the object itself, to allow it to be garbage-collected.
        object = null;

        pool.drain();

        // The discardable reference should be null now.
        Assert.assertNull(discardableReference.get());

        // The object is not (strongly) reachable anymore, so the weak reference may or may not be
        // null (it could be if a GC has happened since the pool was drained).
        // After an explicit GC call it definitely should be null.
        Runtime.getRuntime().gc();

        Assert.assertNull(weakReference.get());
    }

    /**
     * Tests that dropping the (last) discardable reference to an object allows it to be regularly
     * garbage collected.
     */
    @Test
    @RetryOnFailure
    public void testReferenceGCd() {
        DiscardableReferencePool pool = new DiscardableReferencePool();

        Object object = new Object();
        WeakReference<Object> weakReference = new WeakReference<>(object);

        DiscardableReference<Object> discardableReference = pool.put(object);
        Assert.assertEquals(object, discardableReference.get());

        // Drop reference to the object itself and to the discardable reference, allowing the object
        // to be garbage-collected.
        object = null;
        discardableReference = null;

        // The object is not (strongly) reachable anymore, so the weak reference may or may not be
        // null (it could be if a GC has happened since the pool was drained).
        // After an explicit GC call it definitely should be null.
        Runtime.getRuntime().gc();

        Assert.assertNull(weakReference.get());
    }
}
