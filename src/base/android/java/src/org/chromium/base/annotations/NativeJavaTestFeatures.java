// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.annotations;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * NativeJavaTestFeatures is used by the JNI generator in combination with @CalledByNativeJavaTest
 * to generate a native test method that enabled or disables the specified feature for the scope
 * of the test.
 */
public class NativeJavaTestFeatures {
    @Target({ElementType.METHOD})
    @Retention(RetentionPolicy.CLASS)
    public static @interface Enable {
        /**
         *  If present, specifies which features to enable.
         */
        public String[] value() default {};
    }

    @Target({ElementType.METHOD})
    @Retention(RetentionPolicy.CLASS)
    public static @interface Disable {
        /**
         *  If present, specifies which features to disable.
         */
        public String[] value() default {};
    }
}
