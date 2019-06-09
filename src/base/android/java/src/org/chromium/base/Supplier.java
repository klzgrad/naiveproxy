// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

/**
 * Based on Java 8's java.util.function.Supplier.
 * Same as Callable<T>, but without a checked Exception.
 *
 * @param <T> Return type.
 */
public interface Supplier<T> {
    /**
     * Returns a value.
     */
    T get();
}
