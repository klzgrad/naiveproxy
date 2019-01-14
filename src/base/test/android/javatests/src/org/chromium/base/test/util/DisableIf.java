// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * Annotations to support conditional test disabling.
 *
 * These annotations should only be used to disable tests that are temporarily failing
 * in some configurations. If a test should never run at all in some configurations, use
 * {@link Restriction}.
 */
public class DisableIf {

    /** Conditional disabling based on {@link android.os.Build}.
     */
    @Target({ElementType.METHOD, ElementType.TYPE})
    @Retention(RetentionPolicy.RUNTIME)
    public static @interface Build {
        String message() default "";

        int sdk_is_greater_than() default 0;
        int sdk_is_less_than() default Integer.MAX_VALUE;

        String supported_abis_includes() default "";

        String hardware_is() default "";

        String product_name_includes() default "";
    }

    @Target({ElementType.METHOD, ElementType.TYPE})
    @Retention(RetentionPolicy.RUNTIME)
    public static @interface Device {
        /**
         * @return A list of disabled types.
         */
        public String[] type();
    }

    /* Objects of this type should not be created. */
    private DisableIf() {}
}
