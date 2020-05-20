// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.annotations;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * See @CalledByNativeJavaTest. This annotation generates a disabled test instead.
 */
@Target({ElementType.METHOD})
@Retention(RetentionPolicy.CLASS)
public @interface DisabledCalledByNativeJavaTest {
    /*
     *  If present, tells which inner class the method belongs to.
     */
    public String value() default "";
}
