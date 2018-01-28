// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.library_loader;

import android.annotation.SuppressLint;
import android.content.Context;
import android.os.AsyncTask;
import android.os.StrictMode;
import android.os.SystemClock;

import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.SysUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.metrics.RecordHistogram;

import java.util.concurrent.atomic.AtomicBoolean;

import javax.annotation.Nullable;

/**
 * This class provides functionality to load and register the native libraries.
 * Callers are allowed to separate loading the libraries from initializing them.
 * This may be an advantage for Android Webview, where the libraries can be loaded
 * by the zygote process, but then needs per process initialization after the
 * application processes are forked from the zygote process.
 *
 * The libraries may be loaded and initialized from any thread. Synchronization
 * primitives are used to ensure that overlapping requests from different
 * threads are handled sequentially.
 *
 * See also base/android/library_loader/library_loader_hooks.cc, which contains
 * the native counterpart to this class.
 */
@JNINamespace("base::android")
@MainDex
public class LibraryLoader {
    private static final String TAG = "LibraryLoader";

    // Set to true to enable debug logs.
    private static final boolean DEBUG = false;

    // Guards all access to the libraries
    private static final Object sLock = new Object();

    // SharedPreferences key for "don't prefetch libraries" flag
    private static final String DONT_PREFETCH_LIBRARIES_KEY = "dont_prefetch_libraries";

    // The singleton instance of NativeLibraryPreloader.
    private static NativeLibraryPreloader sLibraryPreloader;

    // The singleton instance of LibraryLoader.
    private static volatile LibraryLoader sInstance;

    // One-way switch becomes true when the libraries are loaded.
    private boolean mLoaded;

    // One-way switch becomes true when the Java command line is switched to
    // native.
    private boolean mCommandLineSwitched;

    // One-way switch becomes true when the libraries are initialized (
    // by calling nativeLibraryLoaded, which forwards to LibraryLoaded(...) in
    // library_loader_hooks.cc).
    // Note that this member should remain a one-way switch, since it accessed from multiple
    // threads without a lock.
    private volatile boolean mInitialized;

    // One-way switches recording attempts to use Relro sharing in the browser.
    // The flags are used to report UMA stats later.
    private boolean mIsUsingBrowserSharedRelros;
    private boolean mLoadAtFixedAddressFailed;

    // One-way switch becomes true if the Chromium library was loaded from the
    // APK file directly.
    private boolean mLibraryWasLoadedFromApk;

    // The type of process the shared library is loaded in.
    // This member can be accessed from multiple threads simultaneously, so it have to be
    // final (like now) or be protected in some way (volatile of synchronized).
    private final int mLibraryProcessType;

    // One-way switch that becomes true once
    // {@link asyncPrefetchLibrariesToMemory} has been called.
    private final AtomicBoolean mPrefetchLibraryHasBeenCalled;

    // The number of milliseconds it took to load all the native libraries, which
    // will be reported via UMA. Set once when the libraries are done loading.
    private long mLibraryLoadTimeMs;

    // The return value of NativeLibraryPreloader.loadLibrary(), which will be reported
    // via UMA, it is initialized to the invalid value which shouldn't showup in UMA
    // report.
    private int mLibraryPreloaderStatus = -1;

    /**
     * Set native library preloader, if set, the NativeLibraryPreloader.loadLibrary will be invoked
     * before calling System.loadLibrary, this only applies when not using the chromium linker.
     *
     * @param loader the NativeLibraryPreloader, it shall only be set once and before the
     *               native library loaded.
     */
    public static void setNativeLibraryPreloader(NativeLibraryPreloader loader) {
        synchronized (sLock) {
            assert sLibraryPreloader == null && (sInstance == null || !sInstance.mLoaded);
            sLibraryPreloader = loader;
        }
    }

    /**
     * @param libraryProcessType the process the shared library is loaded in. refer to
     *                           LibraryProcessType for possible values.
     * @return LibraryLoader if existing, otherwise create a new one.
     */
    public static LibraryLoader get(int libraryProcessType) throws ProcessInitException {
        synchronized (sLock) {
            if (sInstance != null) {
                if (sInstance.mLibraryProcessType == libraryProcessType) return sInstance;
                throw new ProcessInitException(
                        LoaderErrors.LOADER_ERROR_NATIVE_LIBRARY_LOAD_FAILED);
            }
            sInstance = new LibraryLoader(libraryProcessType);
            return sInstance;
        }
    }

    private LibraryLoader(int libraryProcessType) {
        mLibraryProcessType = libraryProcessType;
        mPrefetchLibraryHasBeenCalled = new AtomicBoolean();
    }

    /**
     *  This method blocks until the library is fully loaded and initialized.
     */
    public void ensureInitialized() throws ProcessInitException {
        synchronized (sLock) {
            if (mInitialized) {
                // Already initialized, nothing to do.
                return;
            }
            loadAlreadyLocked(ContextUtils.getApplicationContext());
            initializeAlreadyLocked();
        }
    }

    /**
     * Checks if library is fully loaded and initialized.
     */
    public static boolean isInitialized() {
        return sInstance != null && sInstance.mInitialized;
    }

    /**
     * Loads the library and blocks until the load completes. The caller is responsible
     * for subsequently calling ensureInitialized().
     * May be called on any thread, but should only be called once. Note the thread
     * this is called on will be the thread that runs the native code's static initializers.
     * See the comment in doInBackground() for more considerations on this.
     *
     * @throws ProcessInitException if the native library failed to load.
     */
    public void loadNow() throws ProcessInitException {
        loadNowOverrideApplicationContext(ContextUtils.getApplicationContext());
    }

    /**
     * Override kept for callers that need to load from a different app context. Do not use unless
     * specifically required to load from another context that is not the current process's app
     * context.
     *
     * @param appContext The overriding app context to be used to load libraries.
     * @throws ProcessInitException if the native library failed to load with this context.
     */
    public void loadNowOverrideApplicationContext(Context appContext) throws ProcessInitException {
        synchronized (sLock) {
            if (mLoaded && appContext != ContextUtils.getApplicationContext()) {
                throw new IllegalStateException("Attempt to load again from alternate context.");
            }
            loadAlreadyLocked(appContext);
        }
    }

    /**
     * initializes the library here and now: must be called on the thread that the
     * native will call its "main" thread. The library must have previously been
     * loaded with loadNow.
     */
    public void initialize() throws ProcessInitException {
        synchronized (sLock) {
            initializeAlreadyLocked();
        }
    }

    /**
     * Disables prefetching for subsequent runs. The value comes from "DontPrefetchLibraries"
     * finch experiment, and is pushed on every run. I.e. the effect of the finch experiment
     * lags by one run, which is the best we can do considering that prefetching happens way
     * before finch is initialized. Note that since LibraryLoader is in //base, it can't depend
     * on ChromeFeatureList, and has to rely on external code pushing the value.
     *
     * @param dontPrefetch whether not to prefetch libraries
     */
    public static void setDontPrefetchLibrariesOnNextRuns(boolean dontPrefetch) {
        ContextUtils.getAppSharedPreferences()
                .edit()
                .putBoolean(DONT_PREFETCH_LIBRARIES_KEY, dontPrefetch)
                .apply();
    }

    /**
     * @return whether not to prefetch libraries (see setDontPrefetchLibrariesOnNextRun()).
     */
    private static boolean isNotPrefetchingLibraries() {
        // This might be the first time getAppSharedPreferences() is used, so relax strict mode
        // to allow disk reads.
        StrictMode.ThreadPolicy oldPolicy = StrictMode.allowThreadDiskReads();
        try {
            return ContextUtils.getAppSharedPreferences().getBoolean(
                    DONT_PREFETCH_LIBRARIES_KEY, false);
        } finally {
            StrictMode.setThreadPolicy(oldPolicy);
        }
    }

    /** Prefetches the native libraries in a background thread.
     *
     * Launches an AsyncTask that, through a short-lived forked process, reads a
     * part of each page of the native library.  This is done to warm up the
     * page cache, turning hard page faults into soft ones.
     *
     * This is done this way, as testing shows that fadvise(FADV_WILLNEED) is
     * detrimental to the startup time.
     */
    public void asyncPrefetchLibrariesToMemory() {
        SysUtils.logPageFaultCountToTracing();
        if (isNotPrefetchingLibraries()) return;

        final boolean coldStart = mPrefetchLibraryHasBeenCalled.compareAndSet(false, true);
        new AsyncTask<Void, Void, Void>() {
            @Override
            protected Void doInBackground(Void... params) {
                TraceEvent.begin("LibraryLoader.asyncPrefetchLibrariesToMemory");
                int percentage = nativePercentageOfResidentNativeLibraryCode();
                boolean success = false;
                // Arbitrary percentage threshold. If most of the native library is already
                // resident (likely with monochrome), don't bother creating a prefetch process.
                boolean prefetch = coldStart && percentage < 90;
                if (prefetch) {
                    success = nativeForkAndPrefetchNativeLibrary();
                    if (!success) {
                        Log.w(TAG, "Forking a process to prefetch the native library failed.");
                    }
                }
                // As this runs in a background thread, it can be called before histograms are
                // initialized. In this instance, histograms are dropped.
                RecordHistogram.initialize();
                if (prefetch) {
                    RecordHistogram.recordBooleanHistogram("LibraryLoader.PrefetchStatus", success);
                }
                if (percentage != -1) {
                    String histogram = "LibraryLoader.PercentageOfResidentCodeBeforePrefetch"
                            + (coldStart ? ".ColdStartup" : ".WarmStartup");
                    RecordHistogram.recordPercentageHistogram(histogram, percentage);
                }
                TraceEvent.end("LibraryLoader.asyncPrefetchLibrariesToMemory");
                return null;
            }
        }.executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);
    }

    // Helper for loadAlreadyLocked(). Load a native shared library with the Chromium linker.
    // Sets UMA flags depending on the results of loading.
    private void loadLibrary(Linker linker, @Nullable String zipFilePath, String libFilePath) {
        if (linker.isUsingBrowserSharedRelros()) {
            // If the browser is set to attempt shared RELROs then we try first with shared
            // RELROs enabled, and if that fails then retry without.
            mIsUsingBrowserSharedRelros = true;
            try {
                linker.loadLibrary(zipFilePath, libFilePath);
            } catch (UnsatisfiedLinkError e) {
                Log.w(TAG, "Failed to load native library with shared RELRO, retrying without");
                mLoadAtFixedAddressFailed = true;
                linker.loadLibraryNoFixedAddress(zipFilePath, libFilePath);
            }
        } else {
            // No attempt to use shared RELROs in the browser, so load as normal.
            linker.loadLibrary(zipFilePath, libFilePath);
        }

        // Loaded successfully, so record if we loaded directly from an APK.
        if (zipFilePath != null) {
            mLibraryWasLoadedFromApk = true;
        }
    }

    // Invoke either Linker.loadLibrary(...) or System.loadLibrary(...), triggering
    // JNI_OnLoad in native code
    // TODO(crbug.com/635567): Fix this properly.
    @SuppressLint("DefaultLocale")
    private void loadAlreadyLocked(Context appContext) throws ProcessInitException {
        try {
            if (!mLoaded) {
                assert !mInitialized;

                long startTime = SystemClock.uptimeMillis();

                if (Linker.isUsed()) {
                    // Load libraries using the Chromium linker.
                    Linker linker = Linker.getInstance();
                    linker.prepareLibraryLoad();

                    for (String library : NativeLibraries.LIBRARIES) {
                        // Don't self-load the linker. This is because the build system is
                        // not clever enough to understand that all the libraries packaged
                        // in the final .apk don't need to be explicitly loaded.
                        if (linker.isChromiumLinkerLibrary(library)) {
                            if (DEBUG) Log.i(TAG, "ignoring self-linker load");
                            continue;
                        }

                        // Determine where the library should be loaded from.
                        String zipFilePath = null;
                        String libFilePath = System.mapLibraryName(library);
                        if (Linker.isInZipFile()) {
                            // Load directly from the APK.
                            zipFilePath = appContext.getApplicationInfo().sourceDir;
                            Log.i(TAG, "Loading " + library + " from within " + zipFilePath);
                        } else {
                            // The library is in its own file.
                            Log.i(TAG, "Loading " + library);
                        }

                        try {
                            // Load the library using this Linker. May throw UnsatisfiedLinkError.
                            loadLibrary(linker, zipFilePath, libFilePath);
                        } catch (UnsatisfiedLinkError e) {
                            Log.e(TAG, "Unable to load library: " + library);
                            throw(e);
                        }
                    }

                    linker.finishLibraryLoad();
                } else {
                    if (sLibraryPreloader != null) {
                        mLibraryPreloaderStatus = sLibraryPreloader.loadLibrary(appContext);
                    }
                    // Load libraries using the system linker.
                    for (String library : NativeLibraries.LIBRARIES) {
                        try {
                            System.loadLibrary(library);
                        } catch (UnsatisfiedLinkError e) {
                            Log.e(TAG, "Unable to load library: " + library);
                            throw(e);
                        }
                    }
                }

                long stopTime = SystemClock.uptimeMillis();
                mLibraryLoadTimeMs = stopTime - startTime;
                Log.i(TAG, String.format("Time to load native libraries: %d ms (timestamps %d-%d)",
                        mLibraryLoadTimeMs,
                        startTime % 10000,
                        stopTime % 10000));

                mLoaded = true;
            }
        } catch (UnsatisfiedLinkError e) {
            throw new ProcessInitException(LoaderErrors.LOADER_ERROR_NATIVE_LIBRARY_LOAD_FAILED, e);
        }
    }

    // The WebView requires the Command Line to be switched over before
    // initialization is done. This is okay in the WebView's case since the
    // JNI is already loaded by this point.
    public void switchCommandLineForWebView() {
        synchronized (sLock) {
            ensureCommandLineSwitchedAlreadyLocked();
        }
    }

    // Switch the CommandLine over from Java to native if it hasn't already been done.
    // This must happen after the code is loaded and after JNI is ready (since after the
    // switch the Java CommandLine will delegate all calls the native CommandLine).
    private void ensureCommandLineSwitchedAlreadyLocked() {
        assert mLoaded;
        if (mCommandLineSwitched) {
            return;
        }
        CommandLine.enableNativeProxy();
        mCommandLineSwitched = true;
    }

    // Invoke base::android::LibraryLoaded in library_loader_hooks.cc
    private void initializeAlreadyLocked() throws ProcessInitException {
        if (mInitialized) {
            return;
        }

        ensureCommandLineSwitchedAlreadyLocked();

        if (!nativeLibraryLoaded()) {
            Log.e(TAG, "error calling nativeLibraryLoaded");
            throw new ProcessInitException(LoaderErrors.LOADER_ERROR_FAILED_TO_REGISTER_JNI);
        }

        // Check that the version of the library we have loaded matches the version we expect
        Log.i(TAG, String.format("Expected native library version number \"%s\", "
                                   + "actual native library version number \"%s\"",
                           NativeLibraries.sVersionNumber, nativeGetVersionNumber()));
        if (!NativeLibraries.sVersionNumber.equals(nativeGetVersionNumber())) {
            throw new ProcessInitException(LoaderErrors.LOADER_ERROR_NATIVE_LIBRARY_WRONG_VERSION);
        }

        // From now on, keep tracing in sync with native.
        TraceEvent.registerNativeEnabledObserver();

        // From this point on, native code is ready to use and checkIsReady()
        // shouldn't complain from now on (and in fact, it's used by the
        // following calls).
        // Note that this flag can be accessed asynchronously, so any initialization
        // must be performed before.
        mInitialized = true;
    }

    // Called after all native initializations are complete.
    public void onNativeInitializationComplete() {
        recordBrowserProcessHistogram();
    }

    // Record Chromium linker histogram state for the main browser process. Called from
    // onNativeInitializationComplete().
    private void recordBrowserProcessHistogram() {
        if (Linker.getInstance().isUsed()) {
            nativeRecordChromiumAndroidLinkerBrowserHistogram(
                    mIsUsingBrowserSharedRelros,
                    mLoadAtFixedAddressFailed,
                    getLibraryLoadFromApkStatus(),
                    mLibraryLoadTimeMs);
        }
        if (sLibraryPreloader != null) {
            nativeRecordLibraryPreloaderBrowserHistogram(mLibraryPreloaderStatus);
        }
    }

    // Returns the device's status for loading a library directly from the APK file.
    // This method can only be called when the Chromium linker is used.
    private int getLibraryLoadFromApkStatus() {
        assert Linker.getInstance().isUsed();

        if (mLibraryWasLoadedFromApk) {
            return LibraryLoadFromApkStatusCodes.SUCCESSFUL;
        }

        // There were no libraries to be loaded directly from the APK file.
        return LibraryLoadFromApkStatusCodes.UNKNOWN;
    }

    // Register pending Chromium linker histogram state for renderer processes. This cannot be
    // recorded as a histogram immediately because histograms and IPC are not ready at the
    // time it are captured. This function stores a pending value, so that a later call to
    // RecordChromiumAndroidLinkerRendererHistogram() will record it correctly.
    public void registerRendererProcessHistogram(boolean requestedSharedRelro,
                                                 boolean loadAtFixedAddressFailed) {
        if (Linker.getInstance().isUsed()) {
            nativeRegisterChromiumAndroidLinkerRendererHistogram(requestedSharedRelro,
                                                                 loadAtFixedAddressFailed,
                                                                 mLibraryLoadTimeMs);
        }
        if (sLibraryPreloader != null) {
            nativeRegisterLibraryPreloaderRendererHistogram(mLibraryPreloaderStatus);
        }
    }

    /**
     * @return the process the shared library is loaded in, see the LibraryProcessType
     *         for possible values.
     */
    @CalledByNative
    public static int getLibraryProcessType() {
        if (sInstance == null) return LibraryProcessType.PROCESS_UNINITIALIZED;
        return sInstance.mLibraryProcessType;
    }

    /**
     * Override the library loader (normally with a mock) for testing.
     * @param loader the mock library loader.
     */
    @VisibleForTesting
    public static void setLibraryLoaderForTesting(LibraryLoader loader) {
        sInstance = loader;
    }

    // Only methods needed before or during normal JNI registration are during System.OnLoad.
    // nativeLibraryLoaded is then called to register everything else.  This process is called
    // "initialization".  This method will be mapped (by generated code) to the LibraryLoaded
    // definition in base/android/library_loader/library_loader_hooks.cc.
    //
    // Return true on success and false on failure.
    private native boolean nativeLibraryLoaded();

    // Method called to record statistics about the Chromium linker operation for the main
    // browser process. Indicates whether the linker attempted relro sharing for the browser,
    // and if it did, whether the library failed to load at a fixed address. Also records
    // support for loading a library directly from the APK file, and the number of milliseconds
    // it took to load the libraries.
    private native void nativeRecordChromiumAndroidLinkerBrowserHistogram(
            boolean isUsingBrowserSharedRelros,
            boolean loadAtFixedAddressFailed,
            int libraryLoadFromApkStatus,
            long libraryLoadTime);

    // Method called to record the return value of NativeLibraryPreloader.loadLibrary for the main
    // browser process.
    private native void nativeRecordLibraryPreloaderBrowserHistogram(int status);

    // Method called to register (for later recording) statistics about the Chromium linker
    // operation for a renderer process. Indicates whether the linker attempted relro sharing,
    // and if it did, whether the library failed to load at a fixed address. Also records the
    // number of milliseconds it took to load the libraries.
    private native void nativeRegisterChromiumAndroidLinkerRendererHistogram(
            boolean requestedSharedRelro,
            boolean loadAtFixedAddressFailed,
            long libraryLoadTime);

    // Method called to register (for later recording) the return value of
    // NativeLibraryPreloader.loadLibrary for a renderer process.
    private native void nativeRegisterLibraryPreloaderRendererHistogram(int status);

    // Get the version of the native library. This is needed so that we can check we
    // have the right version before initializing the (rest of the) JNI.
    private native String nativeGetVersionNumber();

    // Finds the ranges corresponding to the native library pages, forks a new
    // process to prefetch these pages and waits for it. The new process then
    // terminates. This is blocking.
    private static native boolean nativeForkAndPrefetchNativeLibrary();

    // Returns the percentage of the native library code page that are currently reseident in
    // memory.
    private static native int nativePercentageOfResidentNativeLibraryCode();
}
