// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.support.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.util.DisabledTest;

/**
 * Test class for {@link UserDataHost}.
 */
@RunWith(BaseJUnit4ClassRunner.class)
public class UserDataHostTest {
    private final UserDataHost mHost = new UserDataHost();

    private static class TestObjectA implements UserData {
        private boolean mDestroyed;

        @Override
        public void destroy() {
            mDestroyed = true;
        }

        private boolean isDestroyed() {
            return mDestroyed;
        }
    }

    private static class TestObjectB implements UserData {
        private boolean mDestroyed;

        @Override
        public void destroy() {
            mDestroyed = true;
        }

        private boolean isDestroyed() {
            return mDestroyed;
        }
    }

    private <T extends UserData> void assertGetUserData(Class<T> key) {
        boolean exception = false;
        try {
            mHost.getUserData(key);
        } catch (AssertionError e) {
            exception = true;
        }
        Assert.assertTrue(exception);
    }

    private <T extends UserData> void assertSetUserData(Class<T> key, T obj) {
        boolean exception = false;
        try {
            mHost.setUserData(key, obj);
        } catch (AssertionError e) {
            exception = true;
        }
        Assert.assertTrue(exception);
    }

    private <T extends UserData> void assertRemoveUserData(Class<T> key) {
        boolean exception = false;
        try {
            mHost.removeUserData(key);
        } catch (AssertionError e) {
            exception = true;
        }
        Assert.assertTrue(exception);
    }

    /**
     * Verifies basic operations.
     */
    @Test
    @SmallTest
    @DisabledTest
    public void testBasicOperations() {
        TestObjectA obj = new TestObjectA();
        mHost.setUserData(TestObjectA.class, obj);
        Assert.assertEquals(obj, mHost.getUserData(TestObjectA.class));
        Assert.assertEquals(obj, mHost.removeUserData(TestObjectA.class));
        Assert.assertNull(mHost.getUserData(TestObjectA.class));
        assertRemoveUserData(TestObjectA.class);
    }

    /**
     * Verifies nulled key or data are not allowed.
     */
    @Test
    @SmallTest
    @DisabledTest
    public void testNullKeyOrDataAreDisallowed() {
        TestObjectA obj = new TestObjectA();
        assertSetUserData(null, null);
        assertSetUserData(TestObjectA.class, null);
        assertSetUserData(null, obj);
        assertGetUserData(null);
        assertRemoveUserData(null);
    }

    /**
     * Verifies {@link #setUserData()} overwrites current data.
     */
    @Test
    @SmallTest
    public void testSetUserDataOverwrites() {
        TestObjectA obj1 = new TestObjectA();
        mHost.setUserData(TestObjectA.class, obj1);
        Assert.assertEquals(obj1, mHost.getUserData(TestObjectA.class));

        TestObjectA obj2 = new TestObjectA();
        mHost.setUserData(TestObjectA.class, obj2);
        Assert.assertEquals(obj2, mHost.getUserData(TestObjectA.class));
    }

    /**
     * Verifies {@link UserHostData#destroy()} detroyes each {@link UserData} object.
     */
    @Test
    @SmallTest
    public void testDestroy() {
        TestObjectA objA = new TestObjectA();
        TestObjectB objB = new TestObjectB();
        mHost.setUserData(TestObjectA.class, objA);
        mHost.setUserData(TestObjectB.class, objB);
        Assert.assertEquals(objA, mHost.getUserData(TestObjectA.class));
        Assert.assertEquals(objB, mHost.getUserData(TestObjectB.class));

        mHost.destroy();
        Assert.assertTrue(objA.isDestroyed());
        Assert.assertTrue(objB.isDestroyed());
    }

    /**
     * Verifies that no operation is allowed after {@link #destroy()} is called.
     */
    @Test
    @SmallTest
    @DisabledTest
    public void testOperationsDisallowedAfterDestroy() {
        TestObjectA obj = new TestObjectA();
        mHost.setUserData(TestObjectA.class, obj);
        Assert.assertEquals(obj, mHost.getUserData(TestObjectA.class));

        mHost.destroy();
        assertGetUserData(TestObjectA.class);
        assertSetUserData(TestObjectA.class, obj);
        assertRemoveUserData(TestObjectA.class);
    }
}
