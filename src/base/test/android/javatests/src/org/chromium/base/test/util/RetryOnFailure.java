// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

// Note this annotation may be a NOOP. Check http://crbug.com/797002 for latest status (also see
// http://crbug.com/619055). Current default behavior is to retry all tests on failure.
/**
 * Mark a test as flaky and should be retried on failure. The test is
 * considered passed by the test script if any retry succeeds.
 *
 * Long term, this should be merged with @FlakyTest. But @FlakyTest means
 * has specific meaning that is currently different from RetryOnFailure.
 */
@Target({ElementType.METHOD, ElementType.TYPE})
@Retention(RetentionPolicy.RUNTIME)
public @interface RetryOnFailure {
    String message() default "";
}
