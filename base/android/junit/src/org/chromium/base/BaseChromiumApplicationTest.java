// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.view.KeyEvent;

import org.chromium.base.BaseChromiumApplication.WindowFocusChangedListener;
import org.chromium.testing.local.LocalRobolectricTestRunner;
import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.shadows.ShadowActivity;
import org.robolectric.shadows.multidex.ShadowMultiDex;
import org.robolectric.util.ActivityController;

/** Unit tests for {@link BaseChromiumApplication}. */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE,
        application = BaseChromiumApplication.class,
        shadows = {BaseChromiumApplicationTest.TrackingShadowActivity.class, ShadowMultiDex.class})
public class BaseChromiumApplicationTest {

    /** Shadow that tracks calls to onWindowFocusChanged and dispatchKeyEvent. */
    @Implements(Activity.class)
    public static class TrackingShadowActivity extends ShadowActivity {
        private int mWindowFocusCalls;
        private int mDispatchKeyEventCalls;
        private boolean mReturnValueForKeyDispatch;

        @Implementation
        public void onWindowFocusChanged(@SuppressWarnings("unused") boolean hasFocus) {
            mWindowFocusCalls++;
        }

        @Implementation
        public boolean dispatchKeyEvent(@SuppressWarnings("unused") KeyEvent event) {
            mDispatchKeyEventCalls++;
            return mReturnValueForKeyDispatch;
        }
    }

    @Test
    public void testWindowsFocusChanged() throws Exception {
        BaseChromiumApplication app = (BaseChromiumApplication) RuntimeEnvironment.application;

        WindowFocusChangedListener mock = mock(WindowFocusChangedListener.class);
        app.registerWindowFocusChangedListener(mock);

        ActivityController<Activity> controller =
                Robolectric.buildActivity(Activity.class).create().start().visible();
        TrackingShadowActivity shadow =
                (TrackingShadowActivity) Shadows.shadowOf(controller.get());

        controller.get().getWindow().getCallback().onWindowFocusChanged(true);
        // Assert that listeners were notified.
        verify(mock).onWindowFocusChanged(controller.get(), true);
        // Also ensure that the original activity is forwarded the notification.
        Assert.assertEquals(1, shadow.mWindowFocusCalls);
    }
}
