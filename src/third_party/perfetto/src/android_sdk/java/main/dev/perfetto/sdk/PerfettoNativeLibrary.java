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

package dev.perfetto.sdk;

/**
 * Loads the JNI library backing the native methods in this package.
 *
 * <p>These sources are built twice. The platform build jarjars them into {@code
 * com.android.internal.dev.perfetto.sdk} and pairs them with {@code libperfetto_framework_jni}; the
 * standalone SDK build keeps them in {@code dev.perfetto.sdk} and pairs them with {@code
 * libperfetto_jni}. Both libraries are compiled from the same sources and differ only in the class
 * names their {@code JNI_OnLoad} registers against, aborting if those classes are absent, so the
 * variant has to be picked from the package the classes actually landed in.
 *
 * @hide
 */
final class PerfettoNativeLibrary {
  private static final String FRAMEWORK_PREFIX = "com.android.internal.";

  /** Loads the JNI library matching this build. Repeat calls are no-ops. */
  static void load() {
    boolean isFramework = PerfettoNativeLibrary.class.getName().startsWith(FRAMEWORK_PREFIX);
    System.loadLibrary(isFramework ? "perfetto_framework_jni" : "perfetto_jni");
  }
}
