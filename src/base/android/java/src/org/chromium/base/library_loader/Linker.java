// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.library_loader;

import android.annotation.SuppressLint;
import android.os.Bundle;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.Parcelable;
import android.support.annotation.Nullable;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.StreamUtil;
import org.chromium.base.SysUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.annotations.AccessedByNative;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JniIgnoreNatives;
import org.chromium.base.annotations.MainDex;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/*
 * Technical note:
 *
 * The point of this class is to provide an alternative to System.loadLibrary()
 * to load native shared libraries. One specific feature that it supports is the
 * ability to save RAM by sharing the ELF RELRO sections between renderer
 * processes.
 *
 * When two processes load the same native library at the _same_ memory address,
 * the content of their RELRO section (which includes C++ vtables or any
 * constants that contain pointers) will be largely identical [1].
 *
 * By default, the RELRO section is backed by private RAM in each process,
 * which is still significant on mobile (e.g. 1.28 MB / process on Chrome 30 for
 * Android).
 *
 * However, it is possible to save RAM by creating a shared memory region,
 * copy the RELRO content into it, then have each process swap its private,
 * regular RELRO, with a shared, read-only, mapping of the shared one.
 *
 * This trick saves 98% of the RELRO section size per extra process, after the
 * first one. On the other hand, this requires careful communication between
 * the process where the shared RELRO is created and the one(s) where it is used.
 *
 * Note that swapping the regular RELRO with the shared one is not an atomic
 * operation. Care must be taken that no other thread tries to run native code
 * that accesses it during it. In practice, this means the swap must happen
 * before library native code is executed.
 *
 * [1] The exceptions are pointers to external, randomized, symbols, like
 * those from some system libraries, but these are very few in practice.
 */

/*
 * Security considerations:
 *
 * - Whether the browser process loads its native libraries at the same
 *   addresses as the service ones (to save RAM by sharing the RELRO too)
 *   depends on the configuration variable BROWSER_SHARED_RELRO_CONFIG.
 *
 *   Not using fixed library addresses in the browser process is preferred
 *   for regular devices since it maintains the efficacy of ASLR as an
 *   exploit mitigation across the render <-> browser privilege boundary.
 *
 * - The shared RELRO memory region is always forced read-only after creation,
 *   which means it is impossible for a compromised service process to map
 *   it read-write (e.g. by calling mmap() or mprotect()) and modify its
 *   content, altering values seen in other service processes.
 *
 * - Once the RELRO ashmem region or file is mapped into a service process's
 *   address space, the corresponding file descriptor is immediately closed. The
 *   file descriptor is kept opened in the browser process, because a copy needs
 *   to be sent to each new potential service process.
 *
 * - The common library load addresses are randomized for each instance of
 *   the program on the device. See getRandomBaseLoadAddress() for more
 *   details on how this is obtained.
 *
 * - When loading several libraries in service processes, a simple incremental
 *   approach from the original random base load address is used. This is
 *   sufficient to deal correctly with component builds (which can use dozens
 *   of shared libraries), while regular builds always embed a single shared
 *   library per APK.
 */

/**
 * Here's an explanation of how this class is supposed to be used:
 *
 *  - Native shared libraries should be loaded with Linker.loadLibrary(),
 *    instead of System.loadLibrary(). The two functions should behave the same
 *    (at a high level).
 *
 *  - Before loading any library, prepareLibraryLoad() should be called.
 *
 *  - After loading all libraries, finishLibraryLoad() should be called, before
 *    running any native code from any of the libraries (except their static
 *    constructors, which can't be avoided).
 *
 *  - A service process shall call either initServiceProcess() or
 *    disableSharedRelros() early (i.e. before any loadLibrary() call).
 *    Otherwise, the linker considers that it is running inside the browser
 *    process. This is because various Chromium projects have vastly
 *    different initialization paths.
 *
 *    disableSharedRelros() completely disables shared RELROs, and loadLibrary()
 *    will behave exactly like System.loadLibrary().
 *
 *    initServiceProcess(baseLoadAddress) indicates that shared RELROs are to be
 *    used in this process.
 *
 *  - The browser is in charge of deciding where in memory each library should
 *    be loaded. This address must be passed to each service process (see
 *    ChromiumLinkerParams.java in content for a helper class to do so).
 *
 *  - The browser will also generate shared RELROs for each library it loads.
 *    More specifically, by default when in the browser process, the linker
 *    will:
 *
 *       - Load libraries randomly (just like System.loadLibrary()).
 *       - Compute the fixed address to be used to load the same library
 *         in service processes.
 *       - Create a shared memory region populated with the RELRO region
 *         content pre-relocated for the specific fixed address above.
 *
 *    Note that these shared RELRO regions cannot be used inside the browser
 *    process. They are also never mapped into it.
 *
 *    This behaviour is altered by the BROWSER_SHARED_RELRO_CONFIG configuration
 *    variable below, which may force the browser to load the libraries at
 *    fixed addresses too.
 *
 *  - Once all libraries are loaded in the browser process, one can call
 *    getSharedRelros() which returns a Bundle instance containing a map that
 *    links each loaded library to its shared RELRO region.
 *
 *    This Bundle must be passed to each service process, for example through
 *    a Binder call (note that the Bundle includes file descriptors and cannot
 *    be added as an Intent extra).
 *
 *  - In a service process, finishLibraryLoad() and/or loadLibrary() may
 *    block until the RELRO section Bundle is received. This is typically
 *    done by calling useSharedRelros() from another thread.
 *
 *    This method also ensures the process uses the shared RELROs.
 */
@JniIgnoreNatives
public class Linker {
    // Log tag for this class.
    private static final String TAG = "LibraryLoader";

    // Name of the library that contains our JNI code.
    private static final String LINKER_JNI_LIBRARY = "chromium_android_linker";

    // Constants used to control the behaviour of the browser process with
    // regards to the shared RELRO section.
    //   NEVER        -> The browser never uses it itself.
    //   LOW_RAM_ONLY -> It is only used on devices with low RAM.
    //   ALWAYS       -> It is always used.
    // NOTE: These names are known and expected by the Linker test scripts.
    public static final int BROWSER_SHARED_RELRO_CONFIG_NEVER = 0;
    public static final int BROWSER_SHARED_RELRO_CONFIG_LOW_RAM_ONLY = 1;
    public static final int BROWSER_SHARED_RELRO_CONFIG_ALWAYS = 2;

    // Configuration variable used to control how the browser process uses the
    // shared RELRO. Only change this while debugging linker-related issues.
    // NOTE: This variable's name is known and expected by the Linker test scripts.
    public static final int BROWSER_SHARED_RELRO_CONFIG =
            BROWSER_SHARED_RELRO_CONFIG_LOW_RAM_ONLY;

    // Constants used to control the memory device config. Can be set explicitly
    // by setMemoryDeviceConfigForTesting().
    //   INIT         -> Value is undetermined (will check at runtime).
    //   LOW          -> This is a low-memory device.
    //   NORMAL       -> This is not a low-memory device.
    public static final int MEMORY_DEVICE_CONFIG_INIT = 0;
    public static final int MEMORY_DEVICE_CONFIG_LOW = 1;
    public static final int MEMORY_DEVICE_CONFIG_NORMAL = 2;

    // Indicates if this is a low-memory device or not. The default is to
    // determine this by probing the system at runtime, but this can be forced
    // for testing by calling setMemoryDeviceConfigForTesting().
    private int mMemoryDeviceConfig = MEMORY_DEVICE_CONFIG_INIT;

    // Set to true to enable debug logs.
    protected static final boolean DEBUG = false;

    // Used to pass the shared RELRO Bundle through Binder.
    public static final String EXTRA_LINKER_SHARED_RELROS =
            "org.chromium.base.android.linker.shared_relros";

    // Guards all access to the linker.
    protected final Object mLock = new Object();

    // The name of a class that implements TestRunner.
    private String mTestRunnerClassName;

    // Size of reserved Breakpad guard region. Should match the value of
    // kBreakpadGuardRegionBytes on the JNI side. Used when computing the load
    // addresses of multiple loaded libraries. Set to 0 to disable the guard.
    private static final int BREAKPAD_GUARD_REGION_BYTES = 16 * 1024 * 1024;

    // Size of the area requested when using ASLR to obtain a random load address.
    // Should match the value of kAddressSpaceReservationSize on the JNI side.
    // Used when computing the load addresses of multiple loaded libraries to
    // ensure that we don't try to load outside the area originally requested.
    private static final int ADDRESS_SPACE_RESERVATION = 192 * 1024 * 1024;

    // Becomes true after linker initialization.
    private boolean mInitialized;

    // Set to true if this runs in the browser process. Disabled by initServiceProcess().
    private boolean mInBrowserProcess = true;

    // Becomes true to indicate this process needs to wait for a shared RELRO in
    // finishLibraryLoad().
    private boolean mWaitForSharedRelros;

    // Becomes true when initialization determines that the browser process can use the
    // shared RELRO.
    private boolean mBrowserUsesSharedRelro;

    // The map of all RELRO sections either created or used in this process.
    private Bundle mSharedRelros;

    // Current common random base load address. A value of -1 indicates not yet initialized.
    private long mBaseLoadAddress = -1;

    // Current fixed-location load address for the next library called by loadLibrary().
    // A value of -1 indicates not yet initialized.
    private long mCurrentLoadAddress = -1;

    // Becomes true once prepareLibraryLoad() has been called.
    private boolean mPrepareLibraryLoadCalled;

    // The map of libraries that are currently loaded in this process.
    private HashMap<String, LibInfo> mLoadedLibraries;

    // Singleton.
    private static final Linker sSingleton = new Linker();

    // Private singleton constructor.
    private Linker() {
        // Ensure this class is not referenced unless it's used.
        assert LibraryLoader.useCrazyLinker();
    }

    /**
     * Get singleton instance. Returns a Linker.
     *
     * On N+ Monochrome is selected by Play Store. With Monochrome this code is not used, instead
     * Chrome asks the WebView to provide the library (and the shared RELRO). If the WebView fails
     * to provide the library, the system linker is used as a fallback.
     *
     * Linker runs on all Android releases, but is incompatible with GVR library on N+.
     * Linker is preferred on M- because it does not write the shared RELRO to disk at
     * almost every cold startup.
     *
     * @return the Linker implementation instance.
     */
    public static Linker getInstance() {
        return sSingleton;
    }

    /**
     * Check that native library linker tests are enabled.
     * If not enabled, calls to testing functions will fail with an assertion
     * error.
     *
     * @return true if native library linker tests are enabled.
     */
    public static boolean areTestsEnabled() {
        return NativeLibraries.sEnableLinkerTests;
    }

    /**
     * Assert NativeLibraries.sEnableLinkerTests is true.
     * Hard assertion that we are in a testing context. Cannot be disabled. The
     * test methods in this module permit injection of runnable code by class
     * name. To protect against both malicious and accidental use of these
     * methods, we ensure that NativeLibraries.sEnableLinkerTests is true when
     * any is called.
     */
    private static void assertLinkerTestsAreEnabled() {
        assert NativeLibraries.sEnableLinkerTests : "Testing method called in non-testing context";
    }

    /**
     * A public interface used to run runtime linker tests after loading
     * libraries. Should only be used to implement the linker unit tests,
     * which is controlled by the value of NativeLibraries.sEnableLinkerTests
     * configured at build time.
     */
    public interface TestRunner {
        /**
         * Run runtime checks and return true if they all pass.
         *
         * @param memoryDeviceConfig The current memory device configuration.
         * @param inBrowserProcess true iff this is the browser process.
         * @return true if all checks pass.
         */
        public boolean runChecks(int memoryDeviceConfig, boolean inBrowserProcess);
    }

    /**
     * Call this to retrieve the name of the current TestRunner class name
     * if any. This can be useful to pass it from the browser process to
     * child ones.
     *
     * @return null or a String holding the name of the class implementing
     * the TestRunner set by calling setTestRunnerClassNameForTesting() previously.
     */
    public final String getTestRunnerClassNameForTesting() {
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();

        synchronized (mLock) {
            return mTestRunnerClassName;
        }
    }

    /**
     * Sets the test class name.
     *
     * On the first call, instantiates a Linker and sets its test runner class name. On subsequent
     * calls, checks that the singleton produced by the first call matches the test runner class
     * name.
     */
    public static final void setupForTesting(String testRunnerClassName) {
        if (DEBUG) {
            Log.i(TAG, "setupForTesting(" + testRunnerClassName + ") called");
        }
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();

        synchronized (sSingleton) {
            sSingleton.mTestRunnerClassName = testRunnerClassName;
        }
    }

    /**
     * Instantiate and run the current TestRunner, if any. The TestRunner implementation
     * must be instantiated _after_ all libraries are loaded to ensure that its
     * native methods are properly registered.
     *
     * @param memoryDeviceConfig Linker memory config, or 0 if unused
     * @param inBrowserProcess true if in the browser process
     */
    private final void runTestRunnerClassForTesting(
            int memoryDeviceConfig, boolean inBrowserProcess) {
        if (DEBUG) {
            Log.i(TAG, "runTestRunnerClassForTesting called");
        }
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();

        synchronized (mLock) {
            if (mTestRunnerClassName == null) {
                Log.wtf(TAG, "Linker runtime tests not set up for this process");
                assert false;
            }
            if (DEBUG) {
                Log.i(TAG, "Instantiating " + mTestRunnerClassName);
            }
            TestRunner testRunner = null;
            try {
                testRunner = (TestRunner) Class.forName(mTestRunnerClassName)
                                     .getDeclaredConstructor()
                                     .newInstance();
            } catch (Exception e) {
                Log.wtf(TAG, "Could not instantiate test runner class by name", e);
                assert false;
            }

            if (!testRunner.runChecks(memoryDeviceConfig, inBrowserProcess)) {
                Log.wtf(TAG, "Linker runtime tests failed in this process");
                assert false;
            }

            Log.i(TAG, "All linker tests passed");
        }
    }

    /**
     * Call this method before any other Linker method to force a specific
     * memory device configuration. Should only be used for testing.
     *
     * @param memoryDeviceConfig MEMORY_DEVICE_CONFIG_LOW or MEMORY_DEVICE_CONFIG_NORMAL.
     */
    public final void setMemoryDeviceConfigForTesting(int memoryDeviceConfig) {
        if (DEBUG) {
            Log.i(TAG, "setMemoryDeviceConfigForTesting(" + memoryDeviceConfig + ") called");
        }
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();
        assert memoryDeviceConfig == MEMORY_DEVICE_CONFIG_LOW
                || memoryDeviceConfig == MEMORY_DEVICE_CONFIG_NORMAL;

        synchronized (mLock) {
            assert mMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_INIT;

            mMemoryDeviceConfig = memoryDeviceConfig;
            if (DEBUG) {
                if (mMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_LOW) {
                    Log.i(TAG, "Simulating a low-memory device");
                } else {
                    Log.i(TAG, "Simulating a regular-memory device");
                }
            }
        }
    }

    /**
     * Determine whether a library is the linker library.
     *
     * @param library the name of the library.
     * @return true is the library is the Linker's own JNI library.
     */
    boolean isChromiumLinkerLibrary(String library) {
        return library.equals(LINKER_JNI_LIBRARY);
    }

    /**
     * Load the Linker JNI library. Throws UnsatisfiedLinkError on error.
     */
    @SuppressLint({"UnsafeDynamicallyLoadedCode"})
    private static void loadLinkerJniLibrary() {
        LibraryLoader.setEnvForNative();
        if (DEBUG) {
            String libName = "lib" + LINKER_JNI_LIBRARY + ".so";
            Log.i(TAG, "Loading " + libName);
        }
        try {
            System.loadLibrary(LINKER_JNI_LIBRARY);
            LibraryLoader.incrementRelinkerCountNotHitHistogram();
        } catch (UnsatisfiedLinkError e) {
            if (LibraryLoader.PLATFORM_REQUIRES_NATIVE_FALLBACK_EXTRACTION) {
                System.load(LibraryLoader.getExtractedLibraryPath(
                        ContextUtils.getApplicationContext(), LINKER_JNI_LIBRARY));
                LibraryLoader.incrementRelinkerCountHitHistogram();
            }
        }
    }

    /**
     * Obtain a random base load address at which to place loaded libraries.
     *
     * @return new base load address
     */
    private long getRandomBaseLoadAddress() {
        // nativeGetRandomBaseLoadAddress() returns an address at which it has previously
        // successfully mapped an area larger than the largest library we expect to load,
        // on the basis that we will be able, with high probability, to map our library
        // into it.
        //
        // One issue with this is that we do not yet know the size of the library that
        // we will load is. If it is smaller than the size we used to obtain a random
        // address the library mapping may still succeed. The other issue is that
        // although highly unlikely, there is no guarantee that something else does not
        // map into the area we are going to use between here and when we try to map into it.
        //
        // The above notes mean that all of this is probablistic. It is however okay to do
        // because if, worst case and unlikely, we get unlucky in our choice of address,
        // the back-out and retry without the shared RELRO in the ChildProcessService will
        // keep things running.
        final long address = nativeGetRandomBaseLoadAddress();
        if (DEBUG) {
            Log.i(TAG, String.format(Locale.US, "Random native base load address: 0x%x", address));
        }
        return address;
    }

    /**
     * Load a native shared library with the Chromium linker. Note the crazy linker treats
     * libraries and files as equivalent, so you can only open one library in a given zip
     * file. The library must not be the Chromium linker library.
     *
     * @param libFilePath The path of the library (possibly in the zip file).
     */
    void loadLibrary(String libFilePath) {
        if (DEBUG) {
            Log.i(TAG, "loadLibrary: " + libFilePath);
        }
        final boolean isFixedAddressPermitted = true;
        loadLibraryImpl(libFilePath, isFixedAddressPermitted);
    }

    /**
     * Load a native shared library with the Chromium linker, ignoring any
     * requested fixed address for RELRO sharing. Note the crazy linker treats libraries and
     * files as equivalent, so you can only open one library in a given zip file. The
     * library must not be the Chromium linker library.
     *
     * @param libFilePath The path of the library (possibly in the zip file).
     */
    void loadLibraryNoFixedAddress(String libFilePath) {
        if (DEBUG) {
            Log.i(TAG, "loadLibraryAtAnyAddress: " + libFilePath);
        }
        final boolean isFixedAddressPermitted = false;
        loadLibraryImpl(libFilePath, isFixedAddressPermitted);
    }

    // Used internally to initialize the linker's data. Assumes lock is held.
    // Loads JNI, and sets mMemoryDeviceConfig and mBrowserUsesSharedRelro.
    private void ensureInitializedLocked() {
        assert Thread.holdsLock(mLock);

        if (mInitialized) {
            return;
        }

        // On first call, load libchromium_android_linker.so. Cannot be done in the
        // constructor because instantiation occurs on the UI thread.
        loadLinkerJniLibrary();

        if (mMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_INIT) {
            if (SysUtils.isLowEndDevice()) {
                mMemoryDeviceConfig = MEMORY_DEVICE_CONFIG_LOW;
            } else {
                mMemoryDeviceConfig = MEMORY_DEVICE_CONFIG_NORMAL;
            }
        }

        // Cannot run in the constructor because SysUtils.isLowEndDevice() relies
        // on CommandLine, which may not be available at instantiation.
        switch (BROWSER_SHARED_RELRO_CONFIG) {
            case BROWSER_SHARED_RELRO_CONFIG_NEVER:
                mBrowserUsesSharedRelro = false;
                break;
            case BROWSER_SHARED_RELRO_CONFIG_LOW_RAM_ONLY:
                if (mMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_LOW) {
                    mBrowserUsesSharedRelro = true;
                    Log.w(TAG, "Low-memory device: shared RELROs used in all processes");
                } else {
                    mBrowserUsesSharedRelro = false;
                }
                break;
            case BROWSER_SHARED_RELRO_CONFIG_ALWAYS:
                Log.w(TAG, "Beware: shared RELROs used in all processes!");
                mBrowserUsesSharedRelro = true;
                break;
            default:
                Log.wtf(TAG, "FATAL: illegal shared RELRO config");
                throw new AssertionError();
        }

        mInitialized = true;
    }

    /**
     * Call this method to determine if the linker will try to use shared RELROs
     * for the browser process.
     */
    public boolean isUsingBrowserSharedRelros() {
        synchronized (mLock) {
            ensureInitializedLocked();
            return mInBrowserProcess && mBrowserUsesSharedRelro;
        }
    }

    /**
     * Call this method just before loading any native shared libraries in this process.
     *
     * @param apkFilePath Optional current APK file path. If provided, the linker
     * will try to load libraries directly from it.
     */
    public void prepareLibraryLoad(@Nullable String apkFilePath) {
        if (DEBUG) {
            Log.i(TAG, "prepareLibraryLoad() called");
        }
        synchronized (mLock) {
            ensureInitializedLocked();
            if (apkFilePath != null) {
                nativeAddZipArchivePath(apkFilePath);
            }
            mPrepareLibraryLoadCalled = true;

            if (mInBrowserProcess) {
                // Force generation of random base load address, as well
                // as creation of shared RELRO sections in this process.
                setupBaseLoadAddressLocked();
            }
        }
    }

    /**
     * Call this method just after loading all native shared libraries in this process.
     * Note that when in a service process, this will block until the RELRO bundle is
     * received, i.e. when another thread calls useSharedRelros().
     */
    void finishLibraryLoad() {
        if (DEBUG) {
            Log.i(TAG, "finishLibraryLoad() called");
        }
        synchronized (mLock) {
            ensureInitializedLocked();
            if (DEBUG) {
                Log.i(TAG,
                        String.format(Locale.US,
                                "mInBrowserProcess=%b mBrowserUsesSharedRelro=%b mWaitForSharedRelros=%b",
                                mInBrowserProcess, mBrowserUsesSharedRelro, mWaitForSharedRelros));
            }

            if (mLoadedLibraries == null) {
                if (DEBUG) {
                    Log.i(TAG, "No libraries loaded");
                }
            } else {
                if (mInBrowserProcess) {
                    // Create new Bundle containing RELRO section information
                    // for all loaded libraries. Make it available to getSharedRelros().
                    mSharedRelros = createBundleFromLibInfoMap(mLoadedLibraries);
                    if (DEBUG) {
                        Log.i(TAG, "Shared RELRO created");
                        dumpBundle(mSharedRelros);
                    }

                    if (mBrowserUsesSharedRelro) {
                        useSharedRelrosLocked(mSharedRelros);
                    }
                }

                if (mWaitForSharedRelros) {
                    assert !mInBrowserProcess;

                    // Wait until the shared relro bundle is received from useSharedRelros().
                    while (mSharedRelros == null) {
                        try {
                            mLock.wait();
                        } catch (InterruptedException ie) {
                            // Restore the thread's interrupt status.
                            Thread.currentThread().interrupt();
                        }
                    }
                    useSharedRelrosLocked(mSharedRelros);
                    // Clear the Bundle to ensure its file descriptor references can't be reused.
                    mSharedRelros.clear();
                    mSharedRelros = null;
                }
            }

            // If testing, run tests now that all libraries are loaded and initialized.
            if (NativeLibraries.sEnableLinkerTests) {
                runTestRunnerClassForTesting(mMemoryDeviceConfig, mInBrowserProcess);
            }
        }
        if (DEBUG) {
            Log.i(TAG, "finishLibraryLoad() exiting");
        }
    }

    /**
     * Call this to send a Bundle containing the shared RELRO sections to be
     * used in this process. If initServiceProcess() was previously called,
     * finishLibraryLoad() will not exit until this method is called in another
     * thread with a non-null value.
     *
     * @param bundle The Bundle instance containing a map of shared RELRO sections
     * to use in this process.
     */
    public void useSharedRelros(Bundle bundle) {
        // Ensure the bundle uses the application's class loader, not the framework
        // one which doesn't know anything about LibInfo.
        // Also, hold a fresh copy of it so the caller can't recycle it.
        Bundle clonedBundle = null;
        if (bundle != null) {
            bundle.setClassLoader(LibInfo.class.getClassLoader());
            clonedBundle = new Bundle(LibInfo.class.getClassLoader());
            Parcel parcel = Parcel.obtain();
            bundle.writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            clonedBundle.readFromParcel(parcel);
            parcel.recycle();
        }
        if (DEBUG) {
            Log.i(TAG, "useSharedRelros() called with " + bundle + ", cloned " + clonedBundle);
        }
        synchronized (mLock) {
            // Note that in certain cases, this can be called before
            // initServiceProcess() in service processes.
            mSharedRelros = clonedBundle;
            // Tell any listener blocked in finishLibraryLoad() about it.
            mLock.notifyAll();
        }
    }

    /**
     * Call this to retrieve the shared RELRO sections created in this process,
     * after loading all libraries.
     *
     * @return a new Bundle instance, or null if RELRO sharing is disabled on
     * this system, or if initServiceProcess() was called previously.
     */
    public Bundle getSharedRelros() {
        if (DEBUG) {
            Log.i(TAG, "getSharedRelros() called");
        }
        synchronized (mLock) {
            if (!mInBrowserProcess) {
                if (DEBUG) {
                    Log.i(TAG, "... returning null Bundle");
                }
                return null;
            }

            // Return the Bundle created in finishLibraryLoad().
            if (DEBUG) {
                Log.i(TAG, "... returning " + mSharedRelros);
            }
            return mSharedRelros;
        }
    }

    /**
     * Call this method before loading any libraries to indicate that this
     * process shall neither create or reuse shared RELRO sections.
     */
    public void disableSharedRelros() {
        if (DEBUG) {
            Log.i(TAG, "disableSharedRelros() called");
        }
        synchronized (mLock) {
            ensureInitializedLocked();
            mInBrowserProcess = false;
            mWaitForSharedRelros = false;
            mBrowserUsesSharedRelro = false;
        }
    }

    /**
     * Call this method before loading any libraries to indicate that this
     * process is ready to reuse shared RELRO sections from another one.
     * Typically used when starting service processes.
     *
     * @param baseLoadAddress the base library load address to use.
     */
    public void initServiceProcess(long baseLoadAddress) {
        if (DEBUG) {
            Log.i(TAG,
                    String.format(Locale.US, "initServiceProcess(0x%x) called", baseLoadAddress));
        }
        synchronized (mLock) {
            ensureInitializedLocked();
            mInBrowserProcess = false;
            mBrowserUsesSharedRelro = false;
            mWaitForSharedRelros = true;
            mBaseLoadAddress = baseLoadAddress;
            mCurrentLoadAddress = baseLoadAddress;
        }
    }

    /**
     * Retrieve the base load address of all shared RELRO sections.
     * This also enforces the creation of shared RELRO sections in
     * prepareLibraryLoad(), which can later be retrieved with getSharedRelros().
     *
     * @return a common, random base load address, or 0 if RELRO sharing is
     * disabled.
     */
    public long getBaseLoadAddress() {
        synchronized (mLock) {
            ensureInitializedLocked();
            if (!mInBrowserProcess) {
                Log.w(TAG, "Shared RELRO sections are disabled in this process!");
                return 0;
            }

            setupBaseLoadAddressLocked();
            if (DEBUG) {
                Log.i(TAG,
                        String.format(
                                Locale.US, "getBaseLoadAddress() returns 0x%x", mBaseLoadAddress));
            }
            return mBaseLoadAddress;
        }
    }

    // Used internally to lazily setup the common random base load address.
    private void setupBaseLoadAddressLocked() {
        assert Thread.holdsLock(mLock);
        if (mBaseLoadAddress == -1) {
            mBaseLoadAddress = getRandomBaseLoadAddress();
            mCurrentLoadAddress = mBaseLoadAddress;
            if (mBaseLoadAddress == 0) {
                // If the random address is 0 there are issues with finding enough
                // free address space, so disable RELRO shared / fixed load addresses.
                Log.w(TAG, "Disabling shared RELROs due address space pressure");
                mBrowserUsesSharedRelro = false;
                mWaitForSharedRelros = false;
            }
        }
    }

    // Used for debugging only.
    private void dumpBundle(Bundle bundle) {
        if (DEBUG) {
            Log.i(TAG, "Bundle has " + bundle.size() + " items: " + bundle);
        }
    }

    /**
     * Use the shared RELRO section from a Bundle received form another process.
     * Call this after calling setBaseLoadAddress() then loading all libraries
     * with loadLibrary().
     *
     * @param bundle Bundle instance generated with createSharedRelroBundle() in
     * another process.
     */
    private void useSharedRelrosLocked(Bundle bundle) {
        assert Thread.holdsLock(mLock);

        if (DEBUG) {
            Log.i(TAG, "Linker.useSharedRelrosLocked() called");
        }

        if (bundle == null) {
            if (DEBUG) {
                Log.i(TAG, "null bundle!");
            }
            return;
        }

        if (mLoadedLibraries == null) {
            if (DEBUG) {
                Log.i(TAG, "No libraries loaded!");
            }
            return;
        }

        if (DEBUG) {
            dumpBundle(bundle);
        }
        HashMap<String, LibInfo> relroMap = createLibInfoMapFromBundle(bundle);

        // Apply the RELRO section to all libraries that were already loaded.
        for (Map.Entry<String, LibInfo> entry : relroMap.entrySet()) {
            String libName = entry.getKey();
            LibInfo libInfo = entry.getValue();
            if (!nativeUseSharedRelro(libName, libInfo)) {
                Log.w(TAG, "Could not use shared RELRO section for " + libName);
            } else {
                if (DEBUG) {
                    Log.i(TAG, "Using shared RELRO section for " + libName);
                }
            }
        }

        // In service processes, close all file descriptors from the map now.
        if (!mInBrowserProcess) {
            closeLibInfoMap(relroMap);
        }

        if (DEBUG) {
            Log.i(TAG, "Linker.useSharedRelrosLocked() exiting");
        }
    }

    /**
     * Implements loading a native shared library with the Chromium linker.
     *
     * Load a native shared library with the Chromium linker. If the zip file
     * is not null, the shared library must be uncompressed and page aligned
     * inside the zipfile. Note the crazy linker treats libraries and files as
     * equivalent, so you can only open one library in a given zip file. The
     * library must not be the Chromium linker library.
     *
     * @param libFilePath The path of the library (possibly in the zip file).
     * @param isFixedAddressPermitted If true, uses a fixed load address if one was
     * supplied, otherwise ignores the fixed address and loads wherever available.
     */
    void loadLibraryImpl(String libFilePath, boolean isFixedAddressPermitted) {
        if (DEBUG) {
            Log.i(TAG, "loadLibraryImpl: " + libFilePath + ", " + isFixedAddressPermitted);
        }
        synchronized (mLock) {
            ensureInitializedLocked();

            // Security: Ensure prepareLibraryLoad() was called before.
            // In theory, this can be done lazily here, but it's more consistent
            // to use a pair of functions (i.e. prepareLibraryLoad() + finishLibraryLoad())
            // that wrap all calls to loadLibrary() in the library loader.
            assert mPrepareLibraryLoadCalled;

            if (mLoadedLibraries == null) {
                mLoadedLibraries = new HashMap<String, LibInfo>();
            }

            if (mLoadedLibraries.containsKey(libFilePath)) {
                if (DEBUG) {
                    Log.i(TAG, "Not loading " + libFilePath + " twice");
                }
                return;
            }

            LibInfo libInfo = new LibInfo();
            long loadAddress = 0;
            if (isFixedAddressPermitted) {
                if ((mInBrowserProcess && mBrowserUsesSharedRelro) || mWaitForSharedRelros) {
                    // Load the library at a fixed address.
                    loadAddress = mCurrentLoadAddress;

                    // For multiple libraries, ensure we stay within reservation range.
                    if (loadAddress > mBaseLoadAddress + ADDRESS_SPACE_RESERVATION) {
                        String errorMessage =
                                "Load address outside reservation, for: " + libFilePath;
                        Log.e(TAG, errorMessage);
                        throw new UnsatisfiedLinkError(errorMessage);
                    }
                }
            }

            final String sharedRelRoName = libFilePath;
            if (!nativeLoadLibrary(libFilePath, loadAddress, libInfo)) {
                String errorMessage = "Unable to load library: " + libFilePath;
                Log.e(TAG, errorMessage);
                throw new UnsatisfiedLinkError(errorMessage);
            }

            // Print the load address to the logcat when testing the linker. The format
            // of the string is expected by the Python test_runner script as one of:
            //    BROWSER_LIBRARY_ADDRESS: <library-name> <address>
            //    RENDERER_LIBRARY_ADDRESS: <library-name> <address>
            // Where <library-name> is the library name, and <address> is the hexadecimal load
            // address.
            if (NativeLibraries.sEnableLinkerTests) {
                String tag =
                        mInBrowserProcess ? "BROWSER_LIBRARY_ADDRESS" : "RENDERER_LIBRARY_ADDRESS";
                Log.i(TAG,
                        String.format(
                                Locale.US, "%s: %s %x", tag, libFilePath, libInfo.mLoadAddress));
            }

            if (mInBrowserProcess) {
                // Create a new shared RELRO section at the 'current' fixed load address.
                if (!nativeCreateSharedRelro(sharedRelRoName, mCurrentLoadAddress, libInfo)) {
                    Log.w(TAG,
                            String.format(Locale.US, "Could not create shared RELRO for %s at %x",
                                    libFilePath, mCurrentLoadAddress));
                } else {
                    if (DEBUG) {
                        Log.i(TAG,
                                String.format(Locale.US, "Created shared RELRO for %s at %x: %s",
                                        sharedRelRoName, mCurrentLoadAddress, libInfo.toString()));
                    }
                }
            }

            if (loadAddress != 0 && mCurrentLoadAddress != 0) {
                // Compute the next current load address. If mCurrentLoadAddress
                // is not 0, this is an explicit library load address. Otherwise,
                // this is an explicit load address for relocated RELRO sections
                // only.
                mCurrentLoadAddress =
                        libInfo.mLoadAddress + libInfo.mLoadSize + BREAKPAD_GUARD_REGION_BYTES;
            }

            mLoadedLibraries.put(sharedRelRoName, libInfo);
            if (DEBUG) {
                Log.i(TAG, "Library details " + libInfo.toString());
            }
        }
    }

    /**
     * Record information for a given library.
     * IMPORTANT: Native code knows about this class's fields, so
     * don't change them without modifying the corresponding C++ sources.
     * Also, the LibInfo instance owns the shared RELRO file descriptor.
     */
    private static class LibInfo implements Parcelable {
        LibInfo() {}

        // from Parcelable
        LibInfo(Parcel in) {
            mLoadAddress = in.readLong();
            mLoadSize = in.readLong();
            mRelroStart = in.readLong();
            mRelroSize = in.readLong();
            ParcelFileDescriptor fd = ParcelFileDescriptor.CREATOR.createFromParcel(in);
            // If CreateSharedRelro fails, the OS file descriptor will be -1 and |fd| will be null.
            if (fd != null) {
                mRelroFd = fd.detachFd();
            }
        }

        public void close() {
            if (mRelroFd >= 0) {
                StreamUtil.closeQuietly(ParcelFileDescriptor.adoptFd(mRelroFd));
                mRelroFd = -1;
            }
        }

        // from Parcelable
        @Override
        public void writeToParcel(Parcel out, int flags) {
            if (mRelroFd >= 0) {
                out.writeLong(mLoadAddress);
                out.writeLong(mLoadSize);
                out.writeLong(mRelroStart);
                out.writeLong(mRelroSize);
                try {
                    ParcelFileDescriptor fd = ParcelFileDescriptor.fromFd(mRelroFd);
                    fd.writeToParcel(out, 0);
                    fd.close();
                } catch (java.io.IOException e) {
                    Log.e(TAG, "Can't write LibInfo file descriptor to parcel", e);
                }
            }
        }

        // from Parcelable
        @Override
        public int describeContents() {
            return Parcelable.CONTENTS_FILE_DESCRIPTOR;
        }

        // from Parcelable
        public static final Parcelable.Creator<LibInfo> CREATOR =
                new Parcelable.Creator<LibInfo>() {
                    @Override
                    public LibInfo createFromParcel(Parcel in) {
                        return new LibInfo(in);
                    }

                    @Override
                    public LibInfo[] newArray(int size) {
                        return new LibInfo[size];
                    }
                };

        // IMPORTANT: Don't change these fields without modifying the
        // native code that accesses them directly!
        @AccessedByNative
        public long mLoadAddress; // page-aligned library load address.
        @AccessedByNative
        public long mLoadSize;    // page-aligned library load size.
        @AccessedByNative
        public long mRelroStart;  // page-aligned address in memory, or 0 if none.
        @AccessedByNative
        public long mRelroSize;   // page-aligned size in memory, or 0.
        @AccessedByNative
        public int mRelroFd = -1; // shared RELRO file descriptor, or -1
    }

    // Create a Bundle from a map of LibInfo objects.
    private Bundle createBundleFromLibInfoMap(HashMap<String, LibInfo> map) {
        Bundle bundle = new Bundle(map.size());
        for (Map.Entry<String, LibInfo> entry : map.entrySet()) {
            bundle.putParcelable(entry.getKey(), entry.getValue());
        }
        return bundle;
    }

    // Create a new LibInfo map from a Bundle.
    private HashMap<String, LibInfo> createLibInfoMapFromBundle(Bundle bundle) {
        HashMap<String, LibInfo> map = new HashMap<String, LibInfo>();
        for (String library : bundle.keySet()) {
            LibInfo libInfo = bundle.getParcelable(library);
            map.put(library, libInfo);
        }
        return map;
    }

    // Call the close() method on all values of a LibInfo map.
    private void closeLibInfoMap(HashMap<String, LibInfo> map) {
        for (Map.Entry<String, LibInfo> entry : map.entrySet()) {
            entry.getValue().close();
        }
    }

    /**
     * Move activity from the native thread to the main UI thread.
     * Called from native code on its own thread. Posts a callback from
     * the UI thread back to native code.
     *
     * @param opaque Opaque argument.
     */
    @CalledByNative
    @MainDex
    private static void postCallbackOnMainThread(final long opaque) {
        ThreadUtils.postOnUiThread(new Runnable() {
            @Override
            public void run() {
                nativeRunCallbackOnUiThread(opaque);
            }
        });
    }

    /**
     * Native method to run callbacks on the main UI thread.
     * Supplied by the crazy linker and called by postCallbackOnMainThread.
     *
     * @param opaque Opaque crazy linker arguments.
     */
    private static native void nativeRunCallbackOnUiThread(long opaque);

    /**
     * Native method used to load a library.
     *
     * @param library Platform specific library name (e.g. libfoo.so)
     * @param loadAddress Explicit load address, or 0 for randomized one.
     * @param libInfo If not null, the mLoadAddress and mLoadSize fields
     * of this LibInfo instance will set on success.
     * @return true for success, false otherwise.
     */
    private static native boolean nativeLoadLibrary(
            String library, long loadAddress, LibInfo libInfo);

    /**
     * Native method used to add a zip archive or APK to the search path
     * for native libraries. Allows loading directly from it.
     *
     * @param zipfilePath Path of the zip file containing the libraries.
     * @return true for success, false otherwise.
     */
    private static native boolean nativeAddZipArchivePath(String zipFilePath);

    /**
     * Native method used to create a shared RELRO section.
     * If the library was already loaded at the same address using
     * nativeLoadLibrary(), this creates the RELRO for it. Otherwise,
     * this loads a new temporary library at the specified address,
     * creates and extracts the RELRO section from it, then unloads it.
     *
     * @param library Library name.
     * @param loadAddress load address, which can be different from the one
     * used to load the library in the current process!
     * @param libInfo libInfo instance. On success, the mRelroStart, mRelroSize
     * and mRelroFd will be set.
     * @return true on success, false otherwise.
     */
    private static native boolean nativeCreateSharedRelro(
            String library, long loadAddress, LibInfo libInfo);

    /**
     * Native method used to use a shared RELRO section.
     *
     * @param library Library name.
     * @param libInfo A LibInfo instance containing valid RELRO information
     * @return true on success.
     */
    private static native boolean nativeUseSharedRelro(String library, LibInfo libInfo);

    /**
     * Return a random address that should be free to be mapped with the given size.
     * Maps an area large enough for the largest library we might attempt to load,
     * and if successful then unmaps it and returns the address of the area allocated
     * by the system (with ASLR). The idea is that this area should remain free of
     * other mappings until we map our library into it.
     *
     * @return address to pass to future mmap, or 0 on error.
     */
    private static native long nativeGetRandomBaseLoadAddress();
}
