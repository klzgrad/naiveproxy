// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import android.os.Build;

import junit.framework.TestCase;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;
import org.robolectric.util.ReflectionHelpers;

import org.chromium.base.test.BaseRobolectricTestRunner;

/** Unit tests for the DisableIf annotation and its SkipCheck implementation. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE, sdk = 21)
public class DisableIfTest {
    @Test
    public void testSdkIsLessThanAndIsLessThan() {
        TestCase sdkIsLessThan = new TestCase("sdkIsLessThan") {
            @DisableIf.Build(sdk_is_less_than = 22)
            public void sdkIsLessThan() {}
        };
        Assert.assertTrue(new DisableIfSkipCheck().shouldSkip(sdkIsLessThan));
    }

    @Test
    public void testSdkIsLessThanButIsEqual() {
        TestCase sdkIsEqual = new TestCase("sdkIsEqual") {
            @DisableIf.Build(sdk_is_less_than = 21)
            public void sdkIsEqual() {}
        };
        Assert.assertFalse(new DisableIfSkipCheck().shouldSkip(sdkIsEqual));
    }

    @Test
    public void testSdkIsLessThanButIsGreaterThan() {
        TestCase sdkIsGreaterThan = new TestCase("sdkIsGreaterThan") {
            @DisableIf.Build(sdk_is_less_than = 20)
            public void sdkIsGreaterThan() {}
        };
        Assert.assertFalse(new DisableIfSkipCheck().shouldSkip(sdkIsGreaterThan));
    }

    @Test
    public void testSdkIsGreaterThanButIsLessThan() {
        TestCase sdkIsLessThan = new TestCase("sdkIsLessThan") {
            @DisableIf.Build(sdk_is_greater_than = 22)
            public void sdkIsLessThan() {}
        };
        Assert.assertFalse(new DisableIfSkipCheck().shouldSkip(sdkIsLessThan));
    }

    @Test
    public void testSdkIsGreaterThanButIsEqual() {
        TestCase sdkIsEqual = new TestCase("sdkIsEqual") {
            @DisableIf.Build(sdk_is_greater_than = 21)
            public void sdkIsEqual() {}
        };
        Assert.assertFalse(new DisableIfSkipCheck().shouldSkip(sdkIsEqual));
    }

    @Test
    public void testSdkIsGreaterThanAndIsGreaterThan() {
        TestCase sdkIsGreaterThan = new TestCase("sdkIsGreaterThan") {
            @DisableIf.Build(sdk_is_greater_than = 20)
            public void sdkIsGreaterThan() {}
        };
        Assert.assertTrue(new DisableIfSkipCheck().shouldSkip(sdkIsGreaterThan));
    }

    @Test
    public void testSupportedAbiIncludesAndCpuAbiMatches() {
        TestCase supportedAbisCpuAbiMatch = new TestCase("supportedAbisCpuAbiMatch") {
            @DisableIf.Build(supported_abis_includes = "foo")
            public void supportedAbisCpuAbiMatch() {}
        };
        String[] originalAbis = Build.SUPPORTED_ABIS;
        try {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS",
                    new String[] {"foo", "bar"});
            Assert.assertTrue(new DisableIfSkipCheck().shouldSkip(supportedAbisCpuAbiMatch));
        } finally {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS", originalAbis);
        }
    }

    @Test
    public void testSupportedAbiIncludesAndCpuAbi2Matches() {
        TestCase supportedAbisCpuAbi2Match = new TestCase("supportedAbisCpuAbi2Match") {
            @DisableIf.Build(supported_abis_includes = "bar")
            public void supportedAbisCpuAbi2Match() {}
        };
        String[] originalAbis = Build.SUPPORTED_ABIS;
        try {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS",
                    new String[] {"foo", "bar"});
            Assert.assertTrue(new DisableIfSkipCheck().shouldSkip(supportedAbisCpuAbi2Match));
        } finally {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS", originalAbis);
        }
    }

    @Test
    public void testSupportedAbiIncludesButNoMatch() {
        TestCase supportedAbisNoMatch = new TestCase("supportedAbisNoMatch") {
            @DisableIf.Build(supported_abis_includes = "baz")
            public void supportedAbisNoMatch() {}
        };
        String[] originalAbis = Build.SUPPORTED_ABIS;
        try {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS",
                    new String[] {"foo", "bar"});
            Assert.assertFalse(new DisableIfSkipCheck().shouldSkip(supportedAbisNoMatch));
        } finally {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS", originalAbis);
        }
    }

    @Test
    public void testHardwareIsMatches() {
        TestCase hardwareIsMatches = new TestCase("hardwareIsMatches") {
            @DisableIf.Build(hardware_is = "hammerhead")
            public void hardwareIsMatches() {}
        };
        String originalHardware = Build.HARDWARE;
        try {
            ReflectionHelpers.setStaticField(Build.class, "HARDWARE", "hammerhead");
            Assert.assertTrue(new DisableIfSkipCheck().shouldSkip(hardwareIsMatches));
        } finally {
            ReflectionHelpers.setStaticField(Build.class, "HARDWARE", originalHardware);
        }
    }

    @Test
    public void testHardwareIsDoesntMatch() {
        TestCase hardwareIsDoesntMatch = new TestCase("hardwareIsDoesntMatch") {
            @DisableIf.Build(hardware_is = "hammerhead")
            public void hardwareIsDoesntMatch() {}
        };
        String originalHardware = Build.HARDWARE;
        try {
            ReflectionHelpers.setStaticField(Build.class, "HARDWARE", "mako");
            Assert.assertFalse(new DisableIfSkipCheck().shouldSkip(hardwareIsDoesntMatch));
        } finally {
            ReflectionHelpers.setStaticField(Build.class, "HARDWARE", originalHardware);
        }
    }

    @DisableIf.Build(supported_abis_includes = "foo")
    private static class DisableIfSuperclassTestCase extends TestCase {
        public DisableIfSuperclassTestCase(String name) {
            super(name);
        }
    }

    @DisableIf.Build(hardware_is = "hammerhead")
    private static class DisableIfTestCase extends DisableIfSuperclassTestCase {
        public DisableIfTestCase(String name) {
            super(name);
        }
        public void sampleTestMethod() {}
    }

    @Test
    public void testDisableClass() {
        TestCase sampleTestMethod = new DisableIfTestCase("sampleTestMethod");
        String originalHardware = Build.HARDWARE;
        try {
            ReflectionHelpers.setStaticField(Build.class, "HARDWARE", "hammerhead");
            Assert.assertTrue(new DisableIfSkipCheck().shouldSkip(sampleTestMethod));
        } finally {
            ReflectionHelpers.setStaticField(Build.class, "HARDWARE", originalHardware);
        }
    }

    @Test
    public void testDisableSuperClass() {
        TestCase sampleTestMethod = new DisableIfTestCase("sampleTestMethod");
        String[] originalAbis = Build.SUPPORTED_ABIS;
        try {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS", new String[] {"foo"});
            Assert.assertTrue(new DisableIfSkipCheck().shouldSkip(sampleTestMethod));
        } finally {
            ReflectionHelpers.setStaticField(Build.class, "SUPPORTED_ABIS", originalAbis);
        }
    }
}
