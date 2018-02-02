// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import android.content.ComponentName;
import android.content.Context;
import android.os.Bundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.test.util.Feature;
import org.chromium.testing.local.LocalRobolectricTestRunner;

import java.util.HashSet;
import java.util.Set;

/** Unit tests for the ChildConnectionAllocator class. */
@Config(manifest = Config.NONE)
@RunWith(LocalRobolectricTestRunner.class)
public class ChildConnectionAllocatorTest {
    private static final String TEST_PACKAGE_NAME = "org.chromium.allocator_test";

    private static final int MAX_CONNECTION_NUMBER = 2;

    private static final int FREE_CONNECTION_TEST_CALLBACK_START_FAILED = 1;
    private static final int FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED = 2;

    @Mock
    private ChildProcessConnection.ServiceCallback mServiceCallback;

    static class TestConnectionFactory implements ChildConnectionAllocator.ConnectionFactory {
        private ComponentName mLastServiceName;

        private ChildProcessConnection mConnection;

        private ChildProcessConnection.ServiceCallback mConnectionServiceCallback;

        @Override
        public ChildProcessConnection createConnection(Context context, ComponentName serviceName,
                boolean bindToCaller, boolean bindAsExternalService, Bundle serviceBundle) {
            mLastServiceName = serviceName;
            if (mConnection == null) {
                mConnection = mock(ChildProcessConnection.class);
                // Retrieve the ServiceCallback so we can simulate the service process dying.
                doAnswer(new Answer() {
                    @Override
                    public Object answer(InvocationOnMock invocation) {
                        mConnectionServiceCallback =
                                (ChildProcessConnection.ServiceCallback) invocation.getArgument(1);
                        return null;
                    }
                })
                        .when(mConnection)
                        .start(anyBoolean(), any(ChildProcessConnection.ServiceCallback.class));
            }
            return mConnection;
        }

        public ComponentName getAndResetLastServiceName() {
            ComponentName serviceName = mLastServiceName;
            mLastServiceName = null;
            return serviceName;
        }

        // Use this method to have a callback invoked when the connection is started on the next
        // created connection.
        public void invokeCallbackOnConnectionStart(final boolean onChildStarted,
                final boolean onStartFailed, final boolean onChildProcessDied) {
            final ChildProcessConnection connection = mock(ChildProcessConnection.class);
            mConnection = connection;
            doAnswer(new Answer() {
                @Override
                public Object answer(InvocationOnMock invocation) {
                    ChildProcessConnection.ServiceCallback serviceCallback =
                            (ChildProcessConnection.ServiceCallback) invocation.getArgument(1);
                    if (onChildStarted) {
                        serviceCallback.onChildStarted();
                    }
                    if (onStartFailed) {
                        serviceCallback.onChildStartFailed(connection);
                    }
                    if (onChildProcessDied) {
                        serviceCallback.onChildProcessDied(connection);
                    }
                    return null;
                }
            })
                    .when(mConnection)
                    .start(anyBoolean(), any(ChildProcessConnection.ServiceCallback.class));
        }

        public void simulateServiceStartFailed() {
            mConnectionServiceCallback.onChildStartFailed(mConnection);
        }

        public void simulateServiceProcessDying() {
            mConnectionServiceCallback.onChildProcessDied(mConnection);
        }
    }

    private final TestConnectionFactory mTestConnectionFactory = new TestConnectionFactory();

    private ChildConnectionAllocator mAllocator;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);

        mAllocator = ChildConnectionAllocator.createForTest(TEST_PACKAGE_NAME, "AllocatorTest",
                MAX_CONNECTION_NUMBER, true /* bindToCaller */, false /* bindAsExternalService */,
                false /* useStrongBinding */);
        mAllocator.setConnectionFactoryForTesting(mTestConnectionFactory);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testPlainAllocate() {
        assertFalse(mAllocator.anyConnectionAllocated());
        assertEquals(MAX_CONNECTION_NUMBER, mAllocator.getNumberOfServices());

        ChildConnectionAllocator.Listener listener = mock(ChildConnectionAllocator.Listener.class);
        mAllocator.addListener(listener);

        ChildProcessConnection connection =
                mAllocator.allocate(null /* context */, null /* serviceBundle */, mServiceCallback);
        assertNotNull(connection);

        verify(connection, times(1))
                .start(eq(false) /* useStrongBinding */,
                        any(ChildProcessConnection.ServiceCallback.class));
        verify(listener, times(1)).onConnectionAllocated(mAllocator, connection);
        assertTrue(mAllocator.anyConnectionAllocated());
    }

    /** Tests that different services are created until we reach the max number specified. */
    @Test
    @Feature({"ProcessManagement"})
    public void testAllocateMaxNumber() {
        assertTrue(mAllocator.isFreeConnectionAvailable());
        Set<ComponentName> serviceNames = new HashSet<>();
        for (int i = 0; i < MAX_CONNECTION_NUMBER; i++) {
            ChildProcessConnection connection = mAllocator.allocate(
                    null /* context */, null /* serviceBundle */, mServiceCallback);
            assertNotNull(connection);
            ComponentName serviceName = mTestConnectionFactory.getAndResetLastServiceName();
            assertFalse(serviceNames.contains(serviceName));
            serviceNames.add(serviceName);
        }
        assertFalse(mAllocator.isFreeConnectionAvailable());
        assertNull(mAllocator.allocate(
                null /* context */, null /* serviceBundle */, mServiceCallback));
    }

    /**
     * Tests that the connection is created with the useStrongBinding parameter specified in the
     * allocator.
     */
    @Test
    @Feature({"ProcessManagement"})
    public void testStrongBindingParam() {
        for (boolean useStrongBinding : new boolean[] {true, false}) {
            ChildConnectionAllocator allocator = ChildConnectionAllocator.createForTest(
                    TEST_PACKAGE_NAME, "AllocatorTest", MAX_CONNECTION_NUMBER,
                    true /* bindToCaller */, false /* bindAsExternalService */, useStrongBinding);
            allocator.setConnectionFactoryForTesting(mTestConnectionFactory);
            ChildProcessConnection connection = allocator.allocate(
                    null /* context */, null /* serviceBundle */, mServiceCallback);
            verify(connection, times(0)).start(useStrongBinding, mServiceCallback);
        }
    }

    /**
     * Tests that the various ServiceCallbacks are propagated and posted, so they happen after the
     * ChildProcessAllocator,allocate() method has returned.
     */
    public void runTestWithConnectionCallbacks(
            boolean onChildStarted, boolean onChildStartFailed, boolean onChildProcessDied) {
        // We have to pause the Roboletric looper or it'll execute the posted tasks synchronoulsy.
        ShadowLooper.pauseMainLooper();
        mTestConnectionFactory.invokeCallbackOnConnectionStart(
                onChildStarted, onChildStartFailed, onChildProcessDied);
        ChildProcessConnection connection =
                mAllocator.allocate(null /* context */, null /* serviceBundle */, mServiceCallback);
        assertNotNull(connection);

        // Callbacks are posted.
        verify(mServiceCallback, never()).onChildStarted();
        verify(mServiceCallback, never()).onChildStartFailed(any());
        verify(mServiceCallback, never()).onChildProcessDied(any());
        ShadowLooper.unPauseMainLooper();
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        verify(mServiceCallback, times(onChildStarted ? 1 : 0)).onChildStarted();
        verify(mServiceCallback, times(onChildStartFailed ? 1 : 0)).onChildStartFailed(any());
        verify(mServiceCallback, times(onChildProcessDied ? 1 : 0)).onChildProcessDied(any());
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartedCallback() {
        runTestWithConnectionCallbacks(true /* onChildStarted */, false /* onChildStartFailed */,
                false /* onChildProcessDied */);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartFailedCallback() {
        runTestWithConnectionCallbacks(false /* onChildStarted */, true /* onChildStartFailed */,
                false /* onChildProcessDied */);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildProcessDiedCallback() {
        runTestWithConnectionCallbacks(false /* onChildStarted */, false /* onChildStartFailed */,
                true /* onChildProcessDied */);
    }

    /**
     * Tests that the allocator clears the connection when it fails to bind/process dies and that
     * the listener gets invoked.
     */
    private void testFreeConnection(int callbackType) {
        ChildConnectionAllocator.Listener listener = mock(ChildConnectionAllocator.Listener.class);
        mAllocator.addListener(listener);
        ChildProcessConnection connection =
                mAllocator.allocate(null /* context */, null /* serviceBundle */, mServiceCallback);

        assertNotNull(connection);
        ComponentName serviceName = mTestConnectionFactory.getAndResetLastServiceName();
        verify(connection, times(1))
                .start(eq(false) /* useStrongBinding */,
                        any(ChildProcessConnection.ServiceCallback.class));
        assertTrue(mAllocator.anyConnectionAllocated());
        int onChildStartFailedExpectedCount = 0;
        int onChildProcessDiedExpectedCount = 0;
        switch (callbackType) {
            case FREE_CONNECTION_TEST_CALLBACK_START_FAILED:
                mTestConnectionFactory.simulateServiceStartFailed();
                onChildStartFailedExpectedCount = 1;
                break;
            case FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED:
                mTestConnectionFactory.simulateServiceProcessDying();
                onChildProcessDiedExpectedCount = 1;
                break;
            default:
                fail();
                break;
        }
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertFalse(mAllocator.anyConnectionAllocated());
        verify(listener, times(1)).onConnectionFreed(mAllocator, connection);
        verify(mServiceCallback, never()).onChildStarted();
        verify(mServiceCallback, times(onChildStartFailedExpectedCount))
                .onChildStartFailed(connection);
        verify(mServiceCallback, times(onChildProcessDiedExpectedCount))
                .onChildProcessDied(connection);

        // Allocate a new connection to make sure we are not getting the same connection.
        connection =
                mAllocator.allocate(null /* context */, null /* serviceBundle */, mServiceCallback);
        assertNotNull(connection);
        assertNotEquals(mTestConnectionFactory.getAndResetLastServiceName(), serviceName);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildStartFailed() {
        testFreeConnection(FREE_CONNECTION_TEST_CALLBACK_START_FAILED);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildProcessDied() {
        testFreeConnection(FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED);
    }
}
