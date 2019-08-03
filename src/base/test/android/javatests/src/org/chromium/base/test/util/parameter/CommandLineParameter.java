// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util.parameter;

import java.lang.annotation.ElementType;
import java.lang.annotation.Inherited;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * The annotation for parametering CommandLineFlags in JUnit3 instrumentation tests.
 *
 * E.g. if you add the following annotation to your test class:
 *
 * <code>
 * @CommandLineParameter({"", FLAG_A, FLAG_B})
 * public class MyTestClass
 * </code>
 *
 * The test harness would run the test 3 times with each of the flag added to commandline
 * file.
 */

@Inherited
@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.METHOD, ElementType.TYPE})
public @interface CommandLineParameter {
    String[] value() default {};
}
