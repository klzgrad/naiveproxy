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

package android.os;

/**
 * Host-JVM stub of Android's {@code Process}. Only the surface needed by the
 * perfetto SDK tests is implemented.
 */
public final class Process {
  private Process() {}

  /**
   * Returns the current thread identifier. On a host JVM this is the JVM
   * thread id (not the kernel tid); good enough for tests that only need
   * a stable per-thread integer.
   */
  public static int myTid() {
    return (int) Thread.currentThread().getId();
  }
}
