// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.annotations;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * JCaller is used on the first parameter in a static native function to
 * indicate that the JNI generator should pass in the parameter annotated
 * with JCaller first, as if the function was non-static and being called on
 * the object annotated by JCaller.
 *
 * For example the following functions will call the same cpp method:
 *
 * class A() {
 *    native void nativeFoo(long nativeCppClass);
 * }
 *
 * static native void nativeFoo(@JCaller A a, long nativeCppClass);
 *
 * @NativeMethods
 * interface Natives {
 *    void foo(@JCaller A a, long nativeCppClass);
 * }
 *
 * Will all call CppClass::Foo(JavaParamRef<jobject> jcaller)
 * and jobject will be an instance of A.
 */
@Target(ElementType.PARAMETER)
@Retention(RetentionPolicy.SOURCE)
public @interface JCaller {}
