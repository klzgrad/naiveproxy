// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test;

import org.junit.rules.ExternalResource;

import org.chromium.base.LifetimeAssert;

/**
 * Ensures that all object instances that use LifetimeAssert are destroyed.
 */
public class LifetimeAssertRule extends ExternalResource {
    @Override
    protected void after() {
        LifetimeAssert.assertAllInstancesDestroyedForTesting();
    }
}
