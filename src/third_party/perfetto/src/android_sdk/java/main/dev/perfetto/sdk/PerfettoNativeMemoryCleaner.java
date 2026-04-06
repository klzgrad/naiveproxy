package dev.perfetto.sdk;

import android.system.SystemCleaner;
import dalvik.annotation.optimization.CriticalNative;
import java.lang.ref.Cleaner;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

@SuppressWarnings(
    "AndroidJdkLibsChecker") // Suppress warning about 'java.lang.ref.Cleaner' in google3.
public class PerfettoNativeMemoryCleaner {
  private final AllocationStats mAllocationStats;
  private final Cleaner mCleaner = SystemCleaner.cleaner();

  public PerfettoNativeMemoryCleaner() {
    this(null);
  }

  public PerfettoNativeMemoryCleaner(AllocationStats allocationStats) {
    this.mAllocationStats = allocationStats;
  }

  public void registerNativeAllocation(Object target, long ptr, long freeFunctionPtr) {
    String clsName = target.getClass().getName();
    if (mAllocationStats != null) {
      mAllocationStats.registerAlloc(clsName);
    }
    mCleaner.register(
        target, new FreeNativeMemoryRunnable(ptr, freeFunctionPtr, clsName, mAllocationStats));
  }

  public final boolean isReportAllocationStats() {
    return mAllocationStats != null;
  }

  @CriticalNative
  private static native void applyNativeFunction(long nativeFunction, long nativePtr);

  static final class FreeNativeMemoryRunnable implements Runnable {
    private final long mPtr;
    private final long mFreeFunctionPtr;
    private final String mClsName;
    private final AllocationStats mAllocationStats;

    public FreeNativeMemoryRunnable(
        long ptr, long freeFunctionPtr, String clsName, AllocationStats allocationStats) {
      this.mPtr = ptr;
      this.mFreeFunctionPtr = freeFunctionPtr;
      this.mClsName = clsName;
      this.mAllocationStats = allocationStats;
    }

    @Override
    public void run() {
      if (mAllocationStats != null) {
        mAllocationStats.registerFree(mClsName);
      }
      applyNativeFunction(mFreeFunctionPtr, mPtr);
    }
  }

  /**
   * Holds the count of native memory allocations and de-allocations for each registered class name.
   * Used only in tests.
   */
  public static final class AllocationStats {
    private final Map<String, Integer> allocCount = new HashMap<>();
    private final Map<String, Integer> freeCount = new HashMap<>();

    synchronized void registerAlloc(String target) {
      allocCount.put(target, allocCount.getOrDefault(target, 0) + 1);
    }

    synchronized void registerFree(String target) {
      freeCount.put(target, freeCount.getOrDefault(target, 0) + 1);
    }

    public synchronized int getAllocCountForTarget(String target) {
      Integer count = allocCount.get(target);
      if (count == null) {
        throw new IllegalArgumentException("No allocation count info for '" + target + "'");
      }
      return count;
    }

    public synchronized int getFreeCountForTarget(String target) {
      Integer count = freeCount.get(target);
      if (count == null) {
        throw new IllegalArgumentException("No free count info for '" + target + "'");
      }
      return count;
    }

    public synchronized void reset() {
      allocCount.clear();
      freeCount.clear();
    }

    public synchronized String reportStats() {
      ArrayList<String> lines = new ArrayList<>();
      Set<String> keys = new HashSet<>(allocCount.keySet());
      keys.addAll(freeCount.keySet());
      for (String key : keys) {
        Integer alloc = allocCount.getOrDefault(key, 0);
        Integer free = freeCount.getOrDefault(key, 0);
        String line = key + ": alloc: " + alloc + ", free: " + free;
        lines.add(line);
      }
      return String.join("\n", lines);
    }
  }
}
