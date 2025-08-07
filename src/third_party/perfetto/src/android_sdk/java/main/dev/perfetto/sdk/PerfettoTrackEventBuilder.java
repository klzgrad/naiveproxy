/*
 * Copyright (C) 2025 The Android Open Source Project
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

import dev.perfetto.sdk.PerfettoNativeMemoryCleaner.AllocationStats;
import dev.perfetto.sdk.PerfettoTrace.Category;
import dev.perfetto.sdk.PerfettoTrackEventExtra.ArgBool;
import dev.perfetto.sdk.PerfettoTrackEventExtra.ArgDouble;
import dev.perfetto.sdk.PerfettoTrackEventExtra.ArgInt64;
import dev.perfetto.sdk.PerfettoTrackEventExtra.ArgString;
import dev.perfetto.sdk.PerfettoTrackEventExtra.CounterDouble;
import dev.perfetto.sdk.PerfettoTrackEventExtra.CounterInt64;
import dev.perfetto.sdk.PerfettoTrackEventExtra.CounterTrack;
import dev.perfetto.sdk.PerfettoTrackEventExtra.FieldContainer;
import dev.perfetto.sdk.PerfettoTrackEventExtra.FieldDouble;
import dev.perfetto.sdk.PerfettoTrackEventExtra.FieldInt64;
import dev.perfetto.sdk.PerfettoTrackEventExtra.FieldNested;
import dev.perfetto.sdk.PerfettoTrackEventExtra.FieldString;
import dev.perfetto.sdk.PerfettoTrackEventExtra.Flow;
import dev.perfetto.sdk.PerfettoTrackEventExtra.NamedTrack;
import dev.perfetto.sdk.PerfettoTrackEventExtra.PerfettoPointer;
import dev.perfetto.sdk.PerfettoTrackEventExtra.Proto;
import java.util.ArrayList;
import java.util.function.Supplier;

/** Builder for Perfetto track event extras. */
public final class PerfettoTrackEventBuilder {
  private static final int DEFAULT_EXTRA_CACHE_SIZE = 5;
  private static final int DEFAULT_PENDING_POINTERS_LIST_SIZE = 16;

  private PerfettoTrackEventExtra mExtra;

  private int mTraceType = -1;
  private Category mCategory = null;
  private String mEventName = null;
  private boolean mIsBuilt = false;
  private boolean mIsDebug = false;

  private PerfettoTrackEventBuilder mParent;
  private FieldContainer mCurrentContainer;

  private PerfettoNativeMemoryCleaner mNativeMemoryCleaner;
  private static final PerfettoNativeMemoryCleaner.AllocationStats sNativeAllocationStats =
      new AllocationStats();

  private static final class ObjectsPool {
    public final Pool<FieldInt64> mFieldInt64Pool;
    public final Pool<FieldDouble> mFieldDoublePool;
    public final Pool<FieldString> mFieldStringPool;
    public final Pool<FieldNested> mFieldNestedPool;
    public final Pool<Proto> mProtoPool;

    public ObjectsPool(int capacity) {
      mFieldInt64Pool = new Pool<>(capacity);
      mFieldDoublePool = new Pool<>(capacity);
      mFieldStringPool = new Pool<>(capacity);
      mFieldNestedPool = new Pool<>(capacity);
      mProtoPool = new Pool<>(capacity);
    }

    public void reset() {
      mFieldInt64Pool.reset();
      mFieldDoublePool.reset();
      mFieldStringPool.reset();
      mFieldNestedPool.reset();
      mProtoPool.reset();
    }
  }

  private static final class ObjectsCache {
    public final RingBuffer<NamedTrack> mNamedTrackCache;
    public final RingBuffer<CounterTrack> mCounterTrackCache;
    public final RingBuffer<ArgInt64> mArgInt64Cache;
    public final RingBuffer<ArgBool> mArgBoolCache;
    public final RingBuffer<ArgDouble> mArgDoubleCache;
    public final RingBuffer<ArgString> mArgStringCache;

    public ObjectsCache(int capacity) {
      mNamedTrackCache = new RingBuffer<>(capacity);
      mCounterTrackCache = new RingBuffer<>(capacity);
      mArgInt64Cache = new RingBuffer<>(capacity);
      mArgBoolCache = new RingBuffer<>(capacity);
      mArgDoubleCache = new RingBuffer<>(capacity);
      mArgStringCache = new RingBuffer<>(capacity);
    }
  }

  private static final class LazyInitObjects {
    private CounterInt64 mCounterInt64 = null;
    private CounterDouble mCounterDouble = null;
    private Flow mFlow = null;
    private Flow mTerminatingFlow = null;

    private final PerfettoNativeMemoryCleaner mNativeMemoryCleaner;

    private LazyInitObjects(PerfettoNativeMemoryCleaner memoryCleaner) {
      this.mNativeMemoryCleaner = memoryCleaner;
    }

    public CounterInt64 getCounterInt64() {
      if (mCounterInt64 == null) {
        mCounterInt64 = new CounterInt64(mNativeMemoryCleaner);
      }
      return mCounterInt64;
    }

    public CounterDouble getCounterDouble() {
      if (mCounterDouble == null) {
        mCounterDouble = new CounterDouble(mNativeMemoryCleaner);
      }
      return mCounterDouble;
    }

    public Flow getFlow() {
      if (mFlow == null) {
        mFlow = new Flow(mNativeMemoryCleaner);
      }
      return mFlow;
    }

    public Flow getTerminatingFlow() {
      if (mTerminatingFlow == null) {
        mTerminatingFlow = new Flow(mNativeMemoryCleaner);
      }
      return mTerminatingFlow;
    }
  }

  private Pool<PerfettoTrackEventBuilder> mChildBuildersCache;
  private ObjectsPool mObjectsPool;
  private ObjectsCache mObjectsCache;
  private LazyInitObjects mLazyInitObjects;

  private final boolean mIsCategoryEnabled;

  private ArrayList<PerfettoPointer> mPendingPointers;

  private final Supplier<PerfettoTrackEventBuilder> perfettoTrackEventBuilderSupplier =
      () -> new PerfettoTrackEventBuilder(true, this);
  private final Supplier<FieldNested> fieldNestedSupplier =
      () -> new FieldNested(mNativeMemoryCleaner);
  private final Supplier<Proto> protoSupplier = () -> new Proto(mNativeMemoryCleaner);
  private final Supplier<FieldInt64> fieldInt64Supplier =
      () -> new FieldInt64(mNativeMemoryCleaner);
  private final Supplier<FieldDouble> fieldDoubleSupplier =
      () -> new FieldDouble(mNativeMemoryCleaner);
  private final Supplier<FieldString> fieldStringSupplier =
      () -> new FieldString(mNativeMemoryCleaner);

  private static final PerfettoTrackEventBuilder NO_OP_BUILDER =
      new PerfettoTrackEventBuilder(/* isCategoryEnabled= */ false, /* parent= */ null);

  public static final ThreadLocal<PerfettoTrackEventBuilder> sThreadLocalBuilder =
      ThreadLocal.withInitial(
          () -> new PerfettoTrackEventBuilder(/* isCategoryEnabled= */ true, /* parent= */ null));

  public static PerfettoTrackEventBuilder newEvent(
      int traceType, Category category, boolean isDebug) {
    if (category.isRegistered() && category.isEnabled()) {
      return sThreadLocalBuilder.get().initNewEvent(traceType, category, isDebug);
    }
    return NO_OP_BUILDER;
  }

  private PerfettoTrackEventBuilder(boolean isCategoryEnabled, PerfettoTrackEventBuilder parent) {
    mIsCategoryEnabled = isCategoryEnabled;
    if (!mIsCategoryEnabled) {
      // No fields of this builder will be used, no need to initialize them.
      return;
    }
    if (parent == null) {
      // We are creating a root builder which will be saved in thread local storage.
      mParent = null;
      mNativeMemoryCleaner = new PerfettoNativeMemoryCleaner(null);
      mChildBuildersCache = new Pool<>(DEFAULT_EXTRA_CACHE_SIZE);
      mObjectsPool = new ObjectsPool(DEFAULT_EXTRA_CACHE_SIZE);
      mObjectsCache = new ObjectsCache(DEFAULT_EXTRA_CACHE_SIZE);
      mLazyInitObjects = new LazyInitObjects(mNativeMemoryCleaner);
      mPendingPointers = new ArrayList<>(DEFAULT_PENDING_POINTERS_LIST_SIZE);
    } else {
      // We are create a child builder for proto fields, read all cache fields from the parent.
      mParent = parent;
      mNativeMemoryCleaner = parent.mNativeMemoryCleaner;
      readAllCacheFieldsFromParent(parent);
    }

    mExtra = new PerfettoTrackEventExtra(mNativeMemoryCleaner);
  }

  /** Emits the track event. */
  public void emit() {
    if (!mIsCategoryEnabled) {
      return;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }

    mIsBuilt = true;
    PerfettoTrackEventExtra.native_emit(
        mTraceType, mCategory.getPtr(), mEventName, mExtra.getPtr());
  }

  /** Initialize the builder for a new trace event. */
  private PerfettoTrackEventBuilder initNewEvent(
      int traceType, Category category, boolean isDebug) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    mIsBuilt = false;
    mParent = null;
    mIsDebug = isDebug;
    updateNativeMemoryCleanerForDebug(mIsDebug);
    mTraceType = traceType;
    mCategory = category;
    mEventName = "";

    mExtra.reset();
    mChildBuildersCache.reset();
    mObjectsPool.reset();
    mPendingPointers.clear();
    mCurrentContainer = null;

    return this;
  }

  private PerfettoTrackEventBuilder initChildBuilderForProto(
      PerfettoTrackEventBuilder parent, FieldContainer fieldContainer) {
    mIsBuilt = false;
    mParent = parent;
    mIsDebug = parent.mIsDebug;
    updateNativeMemoryCleanerForDebug(mIsDebug);

    readAllCacheFieldsFromParent(parent);

    mCurrentContainer = fieldContainer;
    return this;
  }

  private void updateNativeMemoryCleanerForDebug(boolean enableDebug) {
    // In current implementation it is possible, that the 'PerfettoTrackEventBuilder' will be
    // used
    // with the 'isDebug' value that differs from the value the builder was created and/or
    // previously used.
    // To correctly handle this situation and to not allocate memory in the fast-path (when the
    // cached builder is used with the same 'isDebug' value), we check the state of the
    // previously
    // created cleaner and create the new one only if 'isDebug' value is updated.
    boolean debugIsAlreadyEnabled = mNativeMemoryCleaner.isReportAllocationStats();
    if (debugIsAlreadyEnabled == enableDebug) {
      return;
    }
    if (enableDebug) {
      mNativeMemoryCleaner = new PerfettoNativeMemoryCleaner(sNativeAllocationStats);
    } else {
      mNativeMemoryCleaner = new PerfettoNativeMemoryCleaner(null);
    }
  }

  private void readAllCacheFieldsFromParent(PerfettoTrackEventBuilder parent) {
    mChildBuildersCache = parent.mChildBuildersCache;
    mObjectsPool = parent.mObjectsPool;
    mObjectsCache = parent.mObjectsCache;
    mLazyInitObjects = parent.mLazyInitObjects;
    mPendingPointers = parent.mPendingPointers;
  }

  /** Sets the event name for the track event. */
  public PerfettoTrackEventBuilder setEventName(String eventName) {
    mEventName = eventName;
    return this;
  }

  /** Adds a debug arg with key {@code name} and value {@code val}. */
  public PerfettoTrackEventBuilder addArg(String name, long val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    ArgInt64 arg = mObjectsCache.mArgInt64Cache.get(name.hashCode());
    if (arg == null || !arg.getName().equals(name)) {
      arg = new ArgInt64(name, mNativeMemoryCleaner);
      mObjectsCache.mArgInt64Cache.put(name.hashCode(), arg);
    }
    arg.setValue(val);
    addPerfettoPointerToExtra(arg);
    return this;
  }

  /** Adds a debug arg with key {@code name} and value {@code val}. */
  public PerfettoTrackEventBuilder addArg(String name, boolean val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    ArgBool arg = mObjectsCache.mArgBoolCache.get(name.hashCode());
    if (arg == null || !arg.getName().equals(name)) {
      arg = new ArgBool(name, mNativeMemoryCleaner);
      mObjectsCache.mArgBoolCache.put(name.hashCode(), arg);
    }
    arg.setValue(val);
    addPerfettoPointerToExtra(arg);
    return this;
  }

  /** Adds a debug arg with key {@code name} and value {@code val}. */
  public PerfettoTrackEventBuilder addArg(String name, double val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    ArgDouble arg = mObjectsCache.mArgDoubleCache.get(name.hashCode());
    if (arg == null || !arg.getName().equals(name)) {
      arg = new ArgDouble(name, mNativeMemoryCleaner);
      mObjectsCache.mArgDoubleCache.put(name.hashCode(), arg);
    }
    arg.setValue(val);
    addPerfettoPointerToExtra(arg);
    return this;
  }

  /** Adds a debug arg with key {@code name} and value {@code val}. */
  public PerfettoTrackEventBuilder addArg(String name, String val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    ArgString arg = mObjectsCache.mArgStringCache.get(name.hashCode());
    if (arg == null || !arg.getName().equals(name)) {
      arg = new ArgString(name, mNativeMemoryCleaner);
      mObjectsCache.mArgStringCache.put(name.hashCode(), arg);
    }
    arg.setValue(val);
    addPerfettoPointerToExtra(arg);
    return this;
  }

  /** Adds a flow with {@code id}. */
  public PerfettoTrackEventBuilder setFlow(long id) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    Flow flow = mLazyInitObjects.getFlow();
    flow.setProcessFlow(id);
    addPerfettoPointerToExtra(flow);
    return this;
  }

  /** Adds a terminating flow with {@code id}. */
  public PerfettoTrackEventBuilder setTerminatingFlow(long id) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    Flow terminatingFlow = mLazyInitObjects.getTerminatingFlow();
    terminatingFlow.setProcessTerminatingFlow(id);
    addPerfettoPointerToExtra(terminatingFlow);
    return this;
  }

  /** Adds the events to a named track instead of the thread track where the event occurred. */
  public PerfettoTrackEventBuilder usingNamedTrack(long parentUuid, String name) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }

    NamedTrack track = mObjectsCache.mNamedTrackCache.get(name.hashCode());
    if (track == null || !track.getName().equals(name)) {
      track = new NamedTrack(name, parentUuid, mNativeMemoryCleaner);
      mObjectsCache.mNamedTrackCache.put(name.hashCode(), track);
    }
    addPerfettoPointerToExtra(track);
    return this;
  }

  /**
   * Adds the events to a process scoped named track instead of the thread track where the event
   * occurred.
   */
  public PerfettoTrackEventBuilder usingProcessNamedTrack(String name) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    return usingNamedTrack(PerfettoTrace.getProcessTrackUuid(), name);
  }

  /**
   * Adds the events to a thread scoped named track instead of the thread track where the event
   * occurred.
   */
  public PerfettoTrackEventBuilder usingThreadNamedTrack(long tid, String name) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    return usingNamedTrack(PerfettoTrace.getThreadTrackUuid(tid), name);
  }

  /** Adds the events to a counter track instead. This is required for setting counter values. */
  public PerfettoTrackEventBuilder usingCounterTrack(long parentUuid, String name) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }

    CounterTrack track = mObjectsCache.mCounterTrackCache.get(name.hashCode());
    if (track == null || !track.getName().equals(name)) {
      track = new CounterTrack(name, parentUuid, mNativeMemoryCleaner);
      mObjectsCache.mCounterTrackCache.put(name.hashCode(), track);
    }
    addPerfettoPointerToExtra(track);
    return this;
  }

  /**
   * Adds the events to a process scoped counter track instead. This is required for setting counter
   * values.
   */
  public PerfettoTrackEventBuilder usingProcessCounterTrack(String name) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    return usingCounterTrack(PerfettoTrace.getProcessTrackUuid(), name);
  }

  /**
   * Adds the events to a thread scoped counter track instead. This is required for setting counter
   * values.
   */
  public PerfettoTrackEventBuilder usingThreadCounterTrack(long tid, String name) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    return usingCounterTrack(PerfettoTrace.getThreadTrackUuid(tid), name);
  }

  /** Sets a long counter value on the event. */
  public PerfettoTrackEventBuilder setCounter(long val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    CounterInt64 counterInt64 = mLazyInitObjects.getCounterInt64();
    counterInt64.setValue(val);
    addPerfettoPointerToExtra(counterInt64);
    return this;
  }

  /** Sets a double counter value on the event. */
  public PerfettoTrackEventBuilder setCounter(double val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }

    CounterDouble counterDouble = mLazyInitObjects.getCounterDouble();
    counterDouble.setValue(val);
    addPerfettoPointerToExtra(counterDouble);
    return this;
  }

  /** Adds a proto field with field id {@code id} and value {@code val}. */
  public PerfettoTrackEventBuilder addField(long id, long val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkBuildingProto();
    }
    FieldInt64 field = mObjectsPool.mFieldInt64Pool.get(fieldInt64Supplier);
    field.setValue(id, val);
    addFieldToContainer(field);
    return this;
  }

  /** Adds a proto field with field id {@code id} and value {@code val}. */
  public PerfettoTrackEventBuilder addField(long id, double val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkBuildingProto();
    }
    FieldDouble field = mObjectsPool.mFieldDoublePool.get(fieldDoubleSupplier);
    field.setValue(id, val);
    addFieldToContainer(field);
    return this;
  }

  /** Adds a proto field with field id {@code id} and value {@code val}. */
  public PerfettoTrackEventBuilder addField(long id, String val) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkBuildingProto();
    }
    FieldString field = mObjectsPool.mFieldStringPool.get(fieldStringSupplier);
    field.setValue(id, val);
    addFieldToContainer(field);
    return this;
  }

  /**
   * Begins a proto field. Fields can be added from this point and there must be a corresponding
   * {@link #endProto}.
   *
   * <p>The proto field is a singleton and all proto fields get added inside the one {@code
   * beginProto} and {@code endProto} within the {@link PerfettoTrackEventBuilder}.
   */
  public PerfettoTrackEventBuilder beginProto() {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkNotBuildingProto();
    }
    Proto proto = mObjectsPool.mProtoPool.get(protoSupplier);
    proto.clearFields();
    addPerfettoPointerToExtra(proto);
    return mChildBuildersCache
        .get(perfettoTrackEventBuilderSupplier)
        .initChildBuilderForProto(this, proto);
  }

  /** Ends a proto field. */
  public PerfettoTrackEventBuilder endProto() {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkMatchingBeginProto();
    }
    return mParent;
  }

  /**
   * Begins a nested proto field with field id {@code id}. Fields can be added from this point and
   * there must be a corresponding {@link #endNested}.
   */
  public PerfettoTrackEventBuilder beginNested(long id) {
    if (!mIsCategoryEnabled) {
      return this;
    }
    if (mIsDebug) {
      checkBuildingProto();
    }
    FieldNested field = mObjectsPool.mFieldNestedPool.get(fieldNestedSupplier);
    field.setId(id);
    addFieldToContainer(field);
    return mChildBuildersCache
        .get(perfettoTrackEventBuilderSupplier)
        .initChildBuilderForProto(this, field);
  }

  /** Ends a nested proto field. */
  public PerfettoTrackEventBuilder endNested() {
    if (!mIsCategoryEnabled) {
      return this;
    }

    if (mIsDebug) {
      checkMatchingBeginNested();
    }

    return mParent;
  }

  private void addFieldToContainer(PerfettoPointer field) {
    // Keep reference to the java object, `mCurrentContainer` uses a native part of the field
    // object.
    mPendingPointers.add(field);
    mCurrentContainer.addField(field);
  }

  private void addPerfettoPointerToExtra(PerfettoPointer arg) {
    // Keep reference to the java object, `mCurrentContainer` uses a native part of the field
    // object.
    mPendingPointers.add(arg);
    mExtra.addPerfettoPointer(arg);
  }

  // Only used in tests.
  public static AllocationStats getNativeAllocationStats() {
    return sNativeAllocationStats;
  }

  /**
   * RingBuffer implemented on top of a SparseArray.
   *
   * <p>Bounds a SparseArray with a FIFO algorithm.
   */
  private static final class RingBuffer<T> {
    private final int mCapacity;
    private final int[] mKeyArray;
    private final T[] mValueArray;
    private int mWriteEnd = 0;

    RingBuffer(int capacity) {
      mCapacity = capacity;
      mKeyArray = new int[capacity];
      mValueArray = (T[]) new Object[capacity];
    }

    public void put(int key, T value) {
      mKeyArray[mWriteEnd] = key;
      mValueArray[mWriteEnd] = value;
      mWriteEnd = (mWriteEnd + 1) % mCapacity;
    }

    public T get(int key) {
      for (int i = 0; i < mCapacity; i++) {
        if (mKeyArray[i] == key) {
          return mValueArray[i];
        }
      }
      return null;
    }
  }

  private static final class Pool<T> {
    private final int mCapacity;
    private final T[] mValueArray;
    private int mIdx = 0;

    Pool(int capacity) {
      mCapacity = capacity;
      mValueArray = (T[]) new Object[capacity];
    }

    public void reset() {
      mIdx = 0;
    }

    public T get(Supplier<T> supplier) {
      if (mIdx >= mCapacity) {
        return supplier.get();
      }
      if (mValueArray[mIdx] == null) {
        mValueArray[mIdx] = supplier.get();
      }
      return mValueArray[mIdx++];
    }
  }

  private void checkState() {
    if (mIsBuilt) {
      throw new IllegalStateException(
          "This builder has already been used. Create a new builder for another event.");
    }
  }

  private boolean isBuildingTopLevelExtra() {
    return mParent == null && mCurrentContainer == null;
  }

  private boolean isBuildingProto() {
    return (mParent != null && mParent.isBuildingTopLevelExtra()) && mCurrentContainer != null;
  }

  private boolean isBuildingNestedProto() {
    return (mParent != null && (mParent.isBuildingProtoOrNestedProto()))
        && mCurrentContainer != null;
  }

  private boolean isBuildingProtoOrNestedProto() {
    return isBuildingProto() || isBuildingNestedProto();
  }

  private void checkNotBuildingProto() {
    checkState();
    if (isBuildingProtoOrNestedProto()) {
      throw new IllegalStateException("Operation not supported for proto.");
    }
  }

  private void checkBuildingProto() {
    checkState();
    if (isBuildingTopLevelExtra()) {
      throw new IllegalStateException("Field operations must be within beginProto/endProto block.");
    }
  }

  private void checkMatchingBeginNested() {
    checkState();
    if (!isBuildingNestedProto()) {
      throw new IllegalStateException("No matching beginNested call.");
    }
  }

  private void checkMatchingBeginProto() {
    checkState();
    if (!isBuildingProto()) {
      throw new IllegalStateException("No matching beginProto call.");
    }
  }
}
