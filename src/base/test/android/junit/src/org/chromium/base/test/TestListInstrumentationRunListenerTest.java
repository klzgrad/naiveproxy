// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test;

import static org.chromium.base.test.TestListInstrumentationRunListener.getAnnotationJSON;
import static org.chromium.base.test.TestListInstrumentationRunListener.getTestMethodJSON;

import org.json.JSONObject;
import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.Description;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.test.util.CommandLineFlags;

import java.util.Arrays;

/**
 * Robolectric test to ensure static methods in TestListInstrumentationRunListener works properly.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class TestListInstrumentationRunListenerTest {
    @CommandLineFlags.Add("hello")
    private static class ParentClass {
        public void testA() {}

        @CommandLineFlags.Add("world")
        public void testB() {}
    }

    @CommandLineFlags.Remove("hello")
    private static class ChildClass extends ParentClass {
    }

    @Test
    public void testGetTestMethodJSON_testA() throws Throwable {
        Description desc = Description.createTestDescription(
                ParentClass.class, "testA",
                ParentClass.class.getMethod("testA").getAnnotations());
        JSONObject json = getTestMethodJSON(desc);
        String expectedJsonString =
                "{"
                + "'method': 'testA',"
                + "'annotations': {}"
                + "}";
        expectedJsonString = expectedJsonString
            .replaceAll("\\s", "")
            .replaceAll("'", "\"");
        Assert.assertEquals(expectedJsonString, json.toString());
    }

    @Test
    public void testGetTestMethodJSON_testB() throws Throwable {
        Description desc = Description.createTestDescription(
                ParentClass.class, "testB",
                ParentClass.class.getMethod("testB").getAnnotations());
        JSONObject json = getTestMethodJSON(desc);
        String expectedJsonString =
                "{"
                + "'method': 'testB',"
                + "'annotations': {"
                + "  'Add': {"
                + "    'value': ['world']"
                + "    }"
                + "  }"
                + "}";
        expectedJsonString = expectedJsonString
            .replaceAll("\\s", "")
            .replaceAll("'", "\"");
        Assert.assertEquals(expectedJsonString, json.toString());
    }


    @Test
    public void testGetTestMethodJSONForInheritedClass() throws Throwable {
        Description desc = Description.createTestDescription(
                ChildClass.class, "testB",
                ChildClass.class.getMethod("testB").getAnnotations());
        JSONObject json = getTestMethodJSON(desc);
        String expectedJsonString =
                "{"
                + "'method': 'testB',"
                + "'annotations': {"
                + "  'Add': {"
                + "    'value': ['world']"
                + "    }"
                + "  }"
                + "}";
        expectedJsonString = expectedJsonString
            .replaceAll("\\s", "")
            .replaceAll("'", "\"");
        Assert.assertEquals(expectedJsonString, json.toString());
    }

    @Test
    public void testGetAnnotationJSONForParentClass() throws Throwable {
        JSONObject json = getAnnotationJSON(Arrays.asList(ParentClass.class.getAnnotations()));
        String expectedJsonString = "{'Add':{'value':['hello']}}";
        expectedJsonString = expectedJsonString
            .replaceAll("\\s", "")
            .replaceAll("'", "\"");
        Assert.assertEquals(expectedJsonString, json.toString());
    }

    @Test
    public void testGetAnnotationJSONForChildClass() throws Throwable {
        JSONObject json = getAnnotationJSON(Arrays.asList(ChildClass.class.getAnnotations()));
        String expectedJsonString = "{'Add':{'value':['hello']},'Remove':{'value':['hello']}}";
        expectedJsonString = expectedJsonString
            .replaceAll("\\s", "")
            .replaceAll("'", "\"");
        Assert.assertEquals(expectedJsonString, json.toString());
    }
}

