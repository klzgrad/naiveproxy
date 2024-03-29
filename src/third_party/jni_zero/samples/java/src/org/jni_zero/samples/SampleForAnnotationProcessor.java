// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.jni_zero.samples;

import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * Sample class that uses the JNI annotation processor for static methods.
 * See generated files at bottom.
 */
class SampleForAnnotationProcessor {
    class TestStruct {
        int mA;
        int mB;
    }
    /**
     * Static methods declared here, each of these refer to a native method
     * which will have its declaration generated by our annotation processor.
     * There will also be a class generated to wrap these native methods
     * with the name SampleForAnnotationProcessorJni which will implement
     * Natives.
     */
    @NativeMethods
    interface Natives {
        void foo();
        SampleForAnnotationProcessor bar(SampleForAnnotationProcessor sample);
        String revString(String stringToReverse);
        String[] sendToNative(String[] strs);
        SampleForAnnotationProcessor[] sendSamplesToNative(SampleForAnnotationProcessor[] strs);
        boolean hasPhalange();

        int[] testAllPrimitives(int zint, int[] ints, long zlong, long[] longs, short zshort,
                short[] shorts, char zchar, char[] chars, byte zbyte, byte[] bytes, double zdouble,
                double[] doubles, float zfloat, float[] floats, boolean zbool, boolean[] bools);

        void testSpecialTypes(
                Class clazz,
                Class[] classes,
                Throwable throwable,
                Throwable[] throwables,
                @JniType("std::string") String string,
                @JniType("std::vector<std::string>") String[] strings,
                TestStruct tStruct,
                TestStruct[] structs,
                @JniType("jni_zero::samples::CPPClass") Object obj,
                @JniType("std::vector<jni_zero::samples::CPPClass>") Object[] objects);

        Throwable returnThrowable();
        Throwable[] returnThrowables();
        Class returnClass();
        Class[] returnClasses();
        String returnString();
        String[] returnStrings();
        TestStruct returnStruct();
        TestStruct[] returnStructs();
        Object returnObject();
        Object[] returnObjects();
    }

    public static void test() {
        int[] x = new int[] {1, 2, 3, 4, 5};
        String[] strs = new String[] {"the", "quick", "brown", "fox"};
        strs = SampleForAnnotationProcessorJni.get().sendToNative(strs);

        SampleForAnnotationProcessor sample =
                SampleForAnnotationProcessorJni.get().bar(new SampleForAnnotationProcessor());
        SampleForAnnotationProcessor[] samples =
                new SampleForAnnotationProcessor[] {sample, sample};
        samples = SampleForAnnotationProcessorJni.get().sendSamplesToNative(samples);

        // Instance of Natives accessed through (classname + "Jni").get().
        SampleForAnnotationProcessorJni.get().foo();
        boolean hasPhalange = SampleForAnnotationProcessorJni.get().hasPhalange();
        String s = SampleForAnnotationProcessorJni.get().revString("abcd");
    }
}
