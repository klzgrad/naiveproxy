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

import com.google.errorprone.annotations.CompileTimeConstant;

import dev.perfetto.sdk.PerfettoTrackEventExtra.NestedTracks;

/**
 * An immutable, reusable handle to a (possibly nested) named track.
 *
 * <p>Build a track once — typically a {@code static final} — and pass it to
 * {@link PerfettoTrackEventBuilder#usingTrack}. A track is rooted at the process,
 * the current thread, or the global scope, and can be nested arbitrarily deep with
 * {@link #child}:
 *
 * <pre>
 *   static final PerfettoTrack RENDER = PerfettoTrack.process("Render");
 *   static final PerfettoTrack GPU = RENDER.child("GPU");
 *   ...
 *   PerfettoTrace.instant(CAT, "frame").usingTrack(GPU).emit();
 * </pre>
 *
 * <p>Emitting on {@code GPU} emits the {@code TrackDescriptor}s for the whole
 * chain (Render under the process, GPU under Render) once per sequence, matching
 * the C SDK's nested-track behaviour. The track uuid is derived natively exactly
 * as the C SDK derives it. Emit cost grows with the nesting depth.
 *
 * <p>By default the UI decides how a track's children are arranged. For explicit
 * control, give the parent {@link #setChildOrdering} {@link
 * #CHILD_ORDERING_EXPLICIT} and each child a {@link #setSiblingOrderRank} (lower
 * sorts first).
 *
 * <p>By default sibling tracks with the same name are merged when displayed. To
 * control it, set {@link #setSiblingMergeBehavior} (e.g. {@link
 * #SIBLING_MERGE_BEHAVIOR_NONE} to never merge, or {@link
 * #SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY} together with {@link
 * #setSiblingMergeKey} to merge by an explicit key instead of by name).
 */
public final class PerfettoTrack {
  // Root scope of the chain. Mirrors RootType in tracing_sdk.h.
  static final int ROOT_GLOBAL = 0;
  static final int ROOT_PROCESS = 1;
  static final int ROOT_THREAD = 2;

  // How a track orders its direct children, for {@link #setChildOrdering}.
  // Values mirror TrackDescriptor.ChildTracksOrdering.
  public static final int CHILD_ORDERING_UNKNOWN = 0;
  public static final int CHILD_ORDERING_LEXICOGRAPHIC = 1;
  public static final int CHILD_ORDERING_CHRONOLOGICAL = 2;
  public static final int CHILD_ORDERING_EXPLICIT = 3;

  // How a track is merged with its eligible siblings, for
  // {@link #setSiblingMergeBehavior}. Values mirror
  // TrackDescriptor.SiblingMergeBehavior.
  public static final int SIBLING_MERGE_BEHAVIOR_UNSPECIFIED = 0;
  public static final int SIBLING_MERGE_BEHAVIOR_BY_TRACK_NAME = 1;
  public static final int SIBLING_MERGE_BEHAVIOR_NONE = 2;
  public static final int SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY = 3;

  // Default per-level id: no disambiguation between same-named sibling tracks.
  private static final long DEFAULT_ID = 0;

  final int mRootType;
  // Per-level state of the chain, outermost (closest to the root) first. The
  // arrays are parallel; each setter clones the array it changes and shares
  // the rest.
  final String[] mNames;
  final long[] mIds;
  // This level's rank among its siblings; 0 means unset.
  final int[] mSiblingOrderRanks;
  // How this level orders its own children; CHILD_ORDERING_UNKNOWN means unset.
  final int[] mChildOrderings;
  // How this level merges with its siblings; SIBLING_MERGE_BEHAVIOR_UNSPECIFIED
  // means unset.
  final int[] mSiblingMergeBehaviors;
  // The per-level merge key, a protobuf oneof: a non-null string key wins over
  // the integer key.
  final String[] mSiblingMergeKeyStrs;
  final long[] mSiblingMergeKeyInts;

  // The handle's native nested-tracks extra, built lazily on first use and held
  // for the handle's lifetime. Freed by the cleaner when the handle is collected.
  private volatile NestedTracks mNested;

  // Frees a handle's native track when the handle is collected. SystemCleaner
  // needs no native lib, so it is safe to hold statically.
  private static final PerfettoNativeMemoryCleaner sCleaner = new PerfettoNativeMemoryCleaner();

  private PerfettoTrack(
      int rootType,
      String[] names,
      long[] ids,
      int[] siblingOrderRanks,
      int[] childOrderings,
      int[] siblingMergeBehaviors,
      String[] siblingMergeKeyStrs,
      long[] siblingMergeKeyInts) {
    mRootType = rootType;
    mNames = names;
    mIds = ids;
    mSiblingOrderRanks = siblingOrderRanks;
    mChildOrderings = childOrderings;
    mSiblingMergeBehaviors = siblingMergeBehaviors;
    mSiblingMergeKeyStrs = siblingMergeKeyStrs;
    mSiblingMergeKeyInts = siblingMergeKeyInts;
  }

  /** A track named {@code name} rooted at {@code rootType}. */
  private static PerfettoTrack root(int rootType, String name) {
    return new PerfettoTrack(
        rootType,
        new String[] {name},
        new long[] {DEFAULT_ID},
        new int[1],
        new int[1],
        new int[1],
        new String[1],
        new long[1]);
  }

  /**
   * The handle's native nested-tracks extra, built once and reused.
   * Package-private; used by {@link PerfettoTrackEventBuilder#usingTrack}.
   *
   * <p>Lock-free lazy init: concurrent first callers may each build an equivalent
   * {@code NestedTracks}, but they are read-only and content-identical (same uuid
   * and descriptor, deduped per-sequence natively), so only one need survive. The
   * {@code volatile} field publishes the winner; the rest are freed by the cleaner.
   */
  NestedTracks nestedTracks() {
    NestedTracks n = mNested;
    if (n == null) {
      n =
          new NestedTracks(
              mRootType,
              mNames,
              mIds,
              mSiblingOrderRanks,
              mChildOrderings,
              mSiblingMergeBehaviors,
              mSiblingMergeKeyStrs,
              mSiblingMergeKeyInts,
              sCleaner);
      mNested = n;
    }
    return n;
  }

  /** A track named {@code name} rooted at the process track. */
  public static PerfettoTrack process(@CompileTimeConstant String name) {
    return root(ROOT_PROCESS, name);
  }

  /** A track named {@code name} rooted at the emitting thread's track. */
  public static PerfettoTrack thread(@CompileTimeConstant String name) {
    return root(ROOT_THREAD, name);
  }

  /** A track named {@code name} rooted at the global scope. */
  public static PerfettoTrack global(@CompileTimeConstant String name) {
    return root(ROOT_GLOBAL, name);
  }

  /** A child track named {@code name} nested under this one. */
  public PerfettoTrack child(@CompileTimeConstant String name) {
    return child(DEFAULT_ID, name);
  }

  /**
   * A child track named {@code name} nested under this one. {@code id} further
   * disambiguates the track from same-named siblings.
   */
  public PerfettoTrack child(long id, @CompileTimeConstant String name) {
    int n = mNames.length;
    String[] names = new String[n + 1];
    long[] ids = new long[n + 1];
    int[] ranks = new int[n + 1];
    int[] orderings = new int[n + 1];
    int[] mergeBehaviors = new int[n + 1];
    String[] mergeKeyStrs = new String[n + 1];
    long[] mergeKeyInts = new long[n + 1];
    System.arraycopy(mNames, 0, names, 0, n);
    System.arraycopy(mIds, 0, ids, 0, n);
    System.arraycopy(mSiblingOrderRanks, 0, ranks, 0, n);
    System.arraycopy(mChildOrderings, 0, orderings, 0, n);
    System.arraycopy(mSiblingMergeBehaviors, 0, mergeBehaviors, 0, n);
    System.arraycopy(mSiblingMergeKeyStrs, 0, mergeKeyStrs, 0, n);
    System.arraycopy(mSiblingMergeKeyInts, 0, mergeKeyInts, 0, n);
    names[n] = name;
    ids[n] = id;
    return new PerfettoTrack(
        mRootType, names, ids, ranks, orderings, mergeBehaviors, mergeKeyStrs, mergeKeyInts);
  }

  /**
   * This track's rank among its siblings; lower sorts first. Only honored when
   * the parent track's {@link #setChildOrdering} is {@link
   * #CHILD_ORDERING_EXPLICIT}.
   */
  public PerfettoTrack setSiblingOrderRank(int rank) {
    int[] ranks = mSiblingOrderRanks.clone();
    ranks[ranks.length - 1] = rank;
    return new PerfettoTrack(
        mRootType,
        mNames,
        mIds,
        ranks,
        mChildOrderings,
        mSiblingMergeBehaviors,
        mSiblingMergeKeyStrs,
        mSiblingMergeKeyInts);
  }

  /**
   * How this track orders its own children, one of the {@code CHILD_ORDERING_*}
   * values.
   */
  public PerfettoTrack setChildOrdering(int childOrdering) {
    int[] orderings = mChildOrderings.clone();
    orderings[orderings.length - 1] = childOrdering;
    return new PerfettoTrack(
        mRootType,
        mNames,
        mIds,
        mSiblingOrderRanks,
        orderings,
        mSiblingMergeBehaviors,
        mSiblingMergeKeyStrs,
        mSiblingMergeKeyInts);
  }

  /**
   * How this track is merged with its eligible siblings, one of the {@code
   * SIBLING_MERGE_BEHAVIOR_*} values.
   */
  public PerfettoTrack setSiblingMergeBehavior(int siblingMergeBehavior) {
    int[] mergeBehaviors = mSiblingMergeBehaviors.clone();
    mergeBehaviors[mergeBehaviors.length - 1] = siblingMergeBehavior;
    return new PerfettoTrack(
        mRootType,
        mNames,
        mIds,
        mSiblingOrderRanks,
        mChildOrderings,
        mergeBehaviors,
        mSiblingMergeKeyStrs,
        mSiblingMergeKeyInts);
  }

  /**
   * The integer key selecting which siblings this track is merged with. Only
   * meaningful when {@link #setSiblingMergeBehavior} is {@link
   * #SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY}. Clears any previously set
   * string key (the two are a protobuf oneof).
   */
  public PerfettoTrack setSiblingMergeKey(long key) {
    String[] mergeKeyStrs = mSiblingMergeKeyStrs.clone();
    long[] mergeKeyInts = mSiblingMergeKeyInts.clone();
    mergeKeyStrs[mergeKeyStrs.length - 1] = null;
    mergeKeyInts[mergeKeyInts.length - 1] = key;
    return new PerfettoTrack(
        mRootType,
        mNames,
        mIds,
        mSiblingOrderRanks,
        mChildOrderings,
        mSiblingMergeBehaviors,
        mergeKeyStrs,
        mergeKeyInts);
  }

  /**
   * The string key selecting which siblings this track is merged with. Only
   * meaningful when {@link #setSiblingMergeBehavior} is {@link
   * #SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY}. Clears any previously set
   * integer key (the two are a protobuf oneof).
   */
  public PerfettoTrack setSiblingMergeKey(String key) {
    String[] mergeKeyStrs = mSiblingMergeKeyStrs.clone();
    long[] mergeKeyInts = mSiblingMergeKeyInts.clone();
    mergeKeyStrs[mergeKeyStrs.length - 1] = key;
    mergeKeyInts[mergeKeyInts.length - 1] = 0;
    return new PerfettoTrack(
        mRootType,
        mNames,
        mIds,
        mSiblingOrderRanks,
        mChildOrderings,
        mSiblingMergeBehaviors,
        mergeKeyStrs,
        mergeKeyInts);
  }
}
