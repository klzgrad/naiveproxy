// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.JniMocker;

/**
 * Tests for {@link AnimationFrameTimeHistogram}
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class AnimationFrameTimeHistogramTest {
    @Rule
    public MockitoRule mockitoRule = MockitoJUnit.rule();

    @Rule
    public JniMocker mocker = new JniMocker();

    @Mock
    AnimationFrameTimeHistogram.Natives mNativeMock;

    @Before
    public void setUp() {
        mocker.mock(AnimationFrameTimeHistogramJni.TEST_HOOKS, mNativeMock);
    }

    @Test
    public void testNatives() {
        AnimationFrameTimeHistogram hist = new AnimationFrameTimeHistogram("histName");
        hist.startRecording();
        hist.endRecording();
        verify(mNativeMock).saveHistogram(eq("histName"), any(long[].class), anyInt());
    }
}
