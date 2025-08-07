/*
 * Copyright (C) 2024 The Android Open Source Project
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

import dalvik.annotation.optimization.CriticalNative;
import dalvik.annotation.optimization.FastNative;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Writes trace events to the perfetto trace buffer. These trace events can be collected and
 * visualized using the Perfetto UI.
 *
 * <p>This tracing mechanism is independent of the method tracing mechanism offered by {@link
 * Debug#startMethodTracing} or {@link Trace}.
 *
 * @hide
 */
public final class PerfettoTrace {
  private static final String TAG = "PerfettoTrace";

  // Keep in sync with C++
  private static final int PERFETTO_TE_TYPE_SLICE_BEGIN = 1;
  private static final int PERFETTO_TE_TYPE_SLICE_END = 2;
  private static final int PERFETTO_TE_TYPE_INSTANT = 3;
  private static final int PERFETTO_TE_TYPE_COUNTER = 4;

  private static boolean sIsDebug = false;
  private static final PerfettoNativeMemoryCleaner sNativeMemoryCleaner =
      new PerfettoNativeMemoryCleaner();

  /** For fetching the next flow event id in a process. */
  private static final AtomicInteger sFlowEventId = new AtomicInteger();

  /**
   * Perfetto category a trace event belongs to. Registering a category is not sufficient to capture
   * events within the category, it must also be enabled in the trace config.
   */
  public static class Category implements PerfettoTrackEventExtra.PerfettoPointer {
    private final String mName;
    private final List<String> mTags;
    private volatile long mPtr;
    private volatile boolean mIsRegistered;

    /**
     * Category ctor.
     *
     * @param name The category name.
     */
    public Category(String name) {
      this(name, List.of());
    }

    /**
     * Category ctor.
     *
     * @param name The category name.
     * @param tags A list of tags associated with this category.
     */
    public Category(String name, List<String> tags) {
      mName = name;
      mTags = tags;
      mPtr = 0;
      mIsRegistered = false;
    }

    @FastNative
    private static native long native_init(String name, String[] tags);

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native void native_register(long ptr);

    @CriticalNative
    private static native void native_unregister(long ptr);

    @CriticalNative
    private static native boolean native_is_enabled(long ptr);

    /** Create the native category object and register it. */
    public synchronized Category register() {
      if (mPtr == 0) {
        long ptr = native_init(mName, mTags.toArray(new String[0]));
        sNativeMemoryCleaner.registerNativeAllocation(this, ptr, native_delete());
        native_register(ptr);
        // There is not much sense in the created, but not yet registered category,
        // so we make the `ptr` visible to other threads only after registration.
        mPtr = ptr;
        mIsRegistered = true;
      } else {
        if (!mIsRegistered) {
          native_register(mPtr);
          mIsRegistered = true;
        }
      }
      return this;
    }

    /** Unregister the category. */
    public synchronized Category unregister() {
      if (mIsRegistered) {
        // mIsRegistered == true implies mPtr != 0
        mIsRegistered = false;
        native_unregister(mPtr);
      }
      return this;
    }

    /** Whether the category is registered and enabled or not. */
    public boolean isEnabled() {
      // mPtr is volatile and is set only from `#register()` method.
      return mPtr != 0 && native_is_enabled(mPtr);
    }

    /** Whether the category is registered or not. */
    public boolean isRegistered() {
      return mIsRegistered;
    }

    /** Returns the pointer to the native category object. */
    @Override
    public long getPtr() {
      // mPtr is volatile and is set only from `#register()` method.
      return mPtr;
    }

    public String getName() {
      return mName;
    }

    public List<String> getTags() {
      return mTags;
    }
  }

  /**
   * Manages a perfetto tracing session. Constructing this object with a config automatically starts
   * a tracing session. Each session must be closed after use and then the resulting trace bytes can
   * be read.
   *
   * <p>The session could be in process or system wide, depending on {@code isBackendInProcess}.
   * This functionality is intended for testing.
   */
  public static final class Session {
    private final long mPtr;

    /** Session ctor. */
    public Session(boolean isBackendInProcess, byte[] config) {
      mPtr = native_start_session(isBackendInProcess, config);
    }

    /** Closes the session and returns the trace. */
    public byte[] close() {
      return native_stop_session(mPtr);
    }
  }

  @CriticalNative
  private static native long native_get_process_track_uuid();

  @CriticalNative
  private static native long native_get_thread_track_uuid(long tid);

  @FastNative
  private static native void native_activate_trigger(String name, int ttlMs);

  @FastNative
  private static native void native_register(boolean isBackendInProcess);

  private static native long native_start_session(boolean isBackendInProcess, byte[] config);

  private static native byte[] native_stop_session(long ptr);

  /**
   * Writes a trace message to indicate a given section of code was invoked.
   *
   * @param category The perfetto category.
   * @param eventName The event name to appear in the trace.
   */
  public static PerfettoTrackEventBuilder instant(Category category, String eventName) {
    return PerfettoTrackEventBuilder.newEvent(PERFETTO_TE_TYPE_INSTANT, category, sIsDebug)
        .setEventName(eventName);
  }

  /**
   * Writes a trace message to indicate the start of a given section of code.
   *
   * @param category The perfetto category.
   * @param eventName The event name to appear in the trace.
   */
  public static PerfettoTrackEventBuilder begin(Category category, String eventName) {
    return PerfettoTrackEventBuilder.newEvent(PERFETTO_TE_TYPE_SLICE_BEGIN, category, sIsDebug)
        .setEventName(eventName);
  }

  /**
   * Writes a trace message to indicate the end of a given section of code.
   *
   * @param category The perfetto category.
   */
  public static PerfettoTrackEventBuilder end(Category category) {
    return PerfettoTrackEventBuilder.newEvent(PERFETTO_TE_TYPE_SLICE_END, category, sIsDebug);
  }

  /**
   * Writes a trace message to indicate the value of a given section of code.
   *
   * @param category The perfetto category.
   * @param value The value of the counter.
   */
  public static PerfettoTrackEventBuilder counter(Category category, long value) {
    return PerfettoTrackEventBuilder.newEvent(PERFETTO_TE_TYPE_COUNTER, category, sIsDebug)
        .setCounter(value);
  }

  /**
   * Writes a trace message to indicate the value of a given section of code.
   *
   * @param category The perfetto category.
   * @param value The value of the counter.
   * @param trackName The trackName for the event.
   */
  public static PerfettoTrackEventBuilder counter(Category category, long value, String trackName) {
    return counter(category, value).usingProcessCounterTrack(trackName);
  }

  /**
   * Writes a trace message to indicate the value of a given section of code.
   *
   * @param category The perfetto category.
   * @param value The value of the counter.
   */
  public static PerfettoTrackEventBuilder counter(Category category, double value) {
    return PerfettoTrackEventBuilder.newEvent(PERFETTO_TE_TYPE_COUNTER, category, sIsDebug)
        .setCounter(value);
  }

  /**
   * Writes a trace message to indicate the value of a given section of code.
   *
   * @param category The perfetto category.
   * @param value The value of the counter.
   * @param trackName The trackName for the event.
   */
  public static PerfettoTrackEventBuilder counter(
      Category category, double value, String trackName) {
    return counter(category, value).usingProcessCounterTrack(trackName);
  }

  /** Returns the next flow id to be used. */
  public static int getFlowId() {
    return sFlowEventId.incrementAndGet();
  }

  /** Returns the global track uuid that can be used as a parent track uuid. */
  public static long getGlobalTrackUuid() {
    return 0;
  }

  /** Returns the process track uuid that can be used as a parent track uuid. */
  public static long getProcessTrackUuid() {
    return native_get_process_track_uuid();
  }

  /** Given a thread tid, returns the thread track uuid that can be used as a parent track uuid. */
  public static long getThreadTrackUuid(long tid) {
    return native_get_thread_track_uuid(tid);
  }

  /** Activates a trigger by name {@code triggerName} with expiry in {@code ttlMs}. */
  public static void activateTrigger(String triggerName, int ttlMs) {
    native_activate_trigger(triggerName, ttlMs);
  }

  /** Registers the process with Perfetto. */
  public static void register(boolean isBackendInProcess) {
    native_register(isBackendInProcess);
  }

  /** Registers the process with Perfetto and enable additional debug checks on the Java side. */
  public static void registerWithDebugChecks(boolean isBackendInProcess) {
    sIsDebug = true;
    register(isBackendInProcess);
  }
}
