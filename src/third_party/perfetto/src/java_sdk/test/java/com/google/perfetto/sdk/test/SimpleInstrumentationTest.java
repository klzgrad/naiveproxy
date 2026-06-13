/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.perfetto.sdk.test;

import static org.junit.Assert.assertNotNull;

import android.util.Log;
import android.content.Context;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.runner.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.junit.Assert;
import java.io.File;

import com.google.perfetto.sdk.PerfettoExampleWrapper;

@RunWith(AndroidJUnit4.class)
public class SimpleInstrumentationTest {

    private static final String TAG = SimpleInstrumentationTest.class.getSimpleName();

    @Test
    public void testDoRunPerfettoMain() {
        Context appContext = InstrumentationRegistry.getInstrumentation().getTargetContext();
        File perfettoOutput = new File(appContext.getFilesDir(), "from_java_example.pftrace");
        String perfettoOutputPath = perfettoOutput.getAbsolutePath();
        Log.i(TAG, "perfettoOutputPath: " + perfettoOutputPath);

        int perfettoResult = PerfettoExampleWrapper.doRunPerfettoMain(perfettoOutputPath);
        Assert.assertEquals(0, perfettoResult);
        Assert.assertTrue(perfettoOutput.exists());
        Assert.assertTrue(perfettoOutput.length() > 0);

        int criticalValuePlusOne = PerfettoExampleWrapper.incrementIntCritical(10);
        Log.i(TAG, "criticalValuePlusOne: " + criticalValuePlusOne);
        Assert.assertEquals(11, criticalValuePlusOne);
    }
}