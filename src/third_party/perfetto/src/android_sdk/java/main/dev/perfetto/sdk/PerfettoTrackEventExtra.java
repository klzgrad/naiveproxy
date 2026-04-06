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
import java.util.concurrent.atomic.AtomicLong;

/**
 * Holds extras to be passed to Perfetto track events in {@link PerfettoTrace}.
 *
 * @hide
 */
final class PerfettoTrackEventExtra {
  private final long mPtr;

  PerfettoTrackEventExtra(PerfettoNativeMemoryCleaner memoryCleaner) {
    mPtr = native_init();
    memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
  }

  /** Returns the native pointer. */
  public long getPtr() {
    return mPtr;
  }

  /** Adds a pointer representing a track event parameter. */
  public void addPerfettoPointer(PerfettoPointer extra) {
    native_add_arg(mPtr, extra.getPtr());
  }

  /** Resets the track event extra. */
  public void reset() {
    native_clear_args(mPtr);
  }

  @CriticalNative
  private static native long native_init();

  @CriticalNative
  private static native long native_delete();

  @CriticalNative
  private static native void native_add_arg(long ptr, long extraPtr);

  @CriticalNative
  private static native void native_clear_args(long ptr);

  @FastNative
  public static native void native_emit(int type, long tag, String name, long ptr);

  /** Represents a native pointer to a Perfetto C SDK struct. E.g. PerfettoTeHlExtra. */
  interface PerfettoPointer {
    /** Returns the perfetto struct native pointer. */
    long getPtr();
  }

  /** Container for {@link Field} instances. */
  interface FieldContainer {
    /** Add {@link Field} to the container. */
    void addField(PerfettoPointer field);
  }

  static final class Flow implements PerfettoPointer {
    private final long mPtr;
    private final long mExtraPtr;

    Flow(PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init();
      mExtraPtr = native_get_extra_ptr(mPtr);
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    public void setProcessFlow(long type) {
      native_set_process_flow(mPtr, type);
    }

    public void setProcessTerminatingFlow(long id) {
      native_set_process_terminating_flow(mPtr, id);
    }

    @Override
    public long getPtr() {
      return mExtraPtr;
    }

    @CriticalNative
    private static native long native_init();

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native void native_set_process_flow(long ptr, long type);

    @CriticalNative
    private static native void native_set_process_terminating_flow(long ptr, long id);

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);
  }

  static final class NamedTrack implements PerfettoPointer {
    private final long mPtr;
    private final long mExtraPtr;
    private final String mName;
    private final long mId;

    NamedTrack(long id, String name, long parentUuid, PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init(id, name, parentUuid);
      mExtraPtr = native_get_extra_ptr(mPtr);
      mName = name;
      mId = id;
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    @Override
    public long getPtr() {
      return mExtraPtr;
    }

    public String getName() {
      return mName;
    }

    @FastNative
    private static native long native_init(long id, String name, long parentUuid);

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);
  }

  static final class CounterTrack implements PerfettoPointer {
    private final long mPtr;
    private final long mExtraPtr;
    private final String mName;

    CounterTrack(String name, long parentUuid, PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init(name, parentUuid);
      mExtraPtr = native_get_extra_ptr(mPtr);
      mName = name;
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    @Override
    public long getPtr() {
      return mExtraPtr;
    }

    public String getName() {
      return mName;
    }

    @FastNative
    private static native long native_init(String name, long parentUuid);

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);
  }

  static final class Counter implements PerfettoPointer {
    private final long mPtr;
    private final long mExtraPtr;

    Counter(PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init();
      mExtraPtr = native_get_extra_ptr(mPtr);
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    @Override
    public long getPtr() {
      return mExtraPtr;
    }

    public void setValueInt64(long value) {
      native_set_value_int64(mPtr, value);
    }

    public void setValueDouble(double value) {
      native_set_value_double(mPtr, value);
    }

    @CriticalNative
    private static native long native_init();

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native void native_set_value_int64(long ptr, long value);

    @CriticalNative
    private static native void native_set_value_double(long ptr, double value);

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);
  }

  static final class Arg implements PerfettoPointer {
    // Private pointer holding Perfetto object with metadata
    private final long mPtr;

    // Public pointer to Perfetto object itself
    private final long mExtraPtr;

    private final String mName;

    Arg(String name, PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init(name);
      mExtraPtr = native_get_extra_ptr(mPtr);
      mName = name;
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    @Override
    public long getPtr() {
      return mExtraPtr;
    }

    public String getName() {
      return mName;
    }

    public void setValueInt64(long val) {
      native_set_value_int64(mPtr, val);
    }

    public void setValueBool(boolean val) {
      native_set_value_bool(mPtr, val);
    }

    public void setValueDouble(double val) {
      native_set_value_double(mPtr, val);
    }

    public void setValueString(String val) {
      native_set_value_string(mPtr, val);
    }

    @FastNative
    private static native long native_init(String name);

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);

    @CriticalNative
    private static native void native_set_value_int64(long ptr, long val);

    @CriticalNative
    private static native void native_set_value_bool(long ptr, boolean val);

    @CriticalNative
    private static native void native_set_value_double(long ptr, double val);

    @FastNative
    private static native void native_set_value_string(long ptr, String val);
  }

  static final class Proto implements PerfettoPointer, FieldContainer {
    // Private pointer holding Perfetto object with metadata
    private final long mPtr;

    // Public pointer to Perfetto object itself
    private final long mExtraPtr;

    Proto(PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init();
      mExtraPtr = native_get_extra_ptr(mPtr);
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    @Override
    public long getPtr() {
      return mExtraPtr;
    }

    @Override
    public void addField(PerfettoPointer field) {
      native_add_field(mPtr, field.getPtr());
    }

    public void clearFields() {
      native_clear_fields(mPtr);
    }

    @CriticalNative
    private static native long native_init();

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);

    @CriticalNative
    private static native void native_add_field(long ptr, long extraPtr);

    @CriticalNative
    private static native void native_clear_fields(long ptr);
  }

  static final class Field implements PerfettoPointer {
    // Private pointer holding Perfetto object with metadata
    private final long mPtr;

    // Public pointer to Perfetto object itself
    private final long mFieldPtr;

    Field(PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init();
      mFieldPtr = native_get_extra_ptr(mPtr);
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    @Override
    public long getPtr() {
      return mFieldPtr;
    }

    public void setValueInt64(long id, long val) {
      native_set_value_int64(mPtr, id, val);
    }

    public void setValueDouble(long id, double val) {
      native_set_value_double(mPtr, id, val);
    }

    public void setValueString(long id, String val) {
      native_set_value_string(mPtr, id, val);
    }

    public void setValueWithInterning(long id, String val, long internedTypeId) {
      native_set_value_with_interning(mPtr, id, val, internedTypeId);
    }

    @CriticalNative
    private static native long native_init();

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);

    @CriticalNative
    private static native void native_set_value_int64(long ptr, long id, long val);

    @CriticalNative
    private static native void native_set_value_double(long ptr, long id, double val);

    @FastNative
    private static native void native_set_value_string(long ptr, long id, String val);

    @FastNative
    private static native void native_set_value_with_interning(
        long ptr, long id, String val, long internedTypeId);
  }

  static final class FieldNested implements PerfettoPointer, FieldContainer {
    // Private pointer holding Perfetto object with metadata
    private final long mPtr;

    // Public pointer to Perfetto object itself
    private final long mFieldPtr;

    FieldNested(PerfettoNativeMemoryCleaner memoryCleaner) {
      mPtr = native_init();
      mFieldPtr = native_get_extra_ptr(mPtr);
      memoryCleaner.registerNativeAllocation(this, mPtr, native_delete());
    }

    @Override
    public long getPtr() {
      return mFieldPtr;
    }

    @Override
    public void addField(PerfettoPointer field) {
      native_add_field(mPtr, field.getPtr());
    }

    public void setId(long id) {
      native_set_id(mPtr, id);
    }

    @CriticalNative
    private static native long native_init();

    @CriticalNative
    private static native long native_delete();

    @CriticalNative
    private static native long native_get_extra_ptr(long ptr);

    @CriticalNative
    private static native void native_add_field(long ptr, long extraPtr);

    @CriticalNative
    private static native void native_set_id(long ptr, long id);
  }
}
