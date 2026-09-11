/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package dalvik.annotation.optimization;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * Host-JVM stub of ART's {@code @CriticalNative}. ART recognises this
 * annotation and elides the JNIEnv pointer and jclass arguments on the C
 * side; non-ART JVMs have no equivalent and ignore it. The C JNI uses
 * {@code PERFETTO_JNI_HOST_PARAMS} to compile with the standard JNI
 * signature when targeting non-ART JVMs, so this stub exists purely to
 * satisfy javac when the perfetto SDK is built for host.
 */
@Retention(RetentionPolicy.CLASS)
@Target(ElementType.METHOD)
public @interface CriticalNative {}
