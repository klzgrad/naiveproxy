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

package android.system;

import java.lang.ref.Cleaner;

/**
 * Host-JVM stub of Android's process-wide cleaner. {@code java.lang.ref.Cleaner}
 * is available on every JDK >= 9, so we simply hand out a per-process singleton.
 */
public final class SystemCleaner {
  private static final Cleaner INSTANCE = Cleaner.create();

  private SystemCleaner() {}

  public static Cleaner cleaner() {
    return INSTANCE;
  }
}
