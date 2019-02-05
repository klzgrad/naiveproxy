// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

/**
 * Implemented by the TEST_HOOKS field in JNI wrapper classes that are generated
 * by the JNI annotation processor. Used in tests for setting the mock
 * implementation of a {@link org.chromium.base.annotations.JniStaticNatives} interface.
 * @param <T> The interface annotated with {@link org.chromium.base.annotations.JniStaticNatives}
 */
public interface JniStaticTestMocker<T> { void setInstanceForTesting(T instance); }
