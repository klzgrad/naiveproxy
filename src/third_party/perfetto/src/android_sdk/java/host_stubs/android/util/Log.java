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

package android.util;

/**
 * Host-JVM stub of Android's logger. Routes to {@code System.err} so test
 * output is captured in the JUnit runner.
 */
public final class Log {
  private Log() {}

  public static int v(String tag, String msg) { return println("V", tag, msg, null); }
  public static int d(String tag, String msg) { return println("D", tag, msg, null); }
  public static int i(String tag, String msg) { return println("I", tag, msg, null); }
  public static int w(String tag, String msg) { return println("W", tag, msg, null); }
  public static int w(String tag, String msg, Throwable tr) { return println("W", tag, msg, tr); }
  public static int e(String tag, String msg) { return println("E", tag, msg, null); }
  public static int e(String tag, String msg, Throwable tr) { return println("E", tag, msg, tr); }

  private static int println(String level, String tag, String msg, Throwable tr) {
    System.err.println(level + "/" + tag + ": " + msg);
    if (tr != null) tr.printStackTrace(System.err);
    return 0;
  }
}
