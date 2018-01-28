// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.library_loader;

import android.os.Build;
import android.os.Bundle;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.Parcelable;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.annotations.AccessedByNative;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

import javax.annotation.Nullable;

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
public abstract class Linker {
    // Log tag for this class.
    private static final String TAG = "LibraryLoader";

    // Name of the library that contains our JNI code.
    private static final String LINKER_JNI_LIBRARY = "chromium_android_linker";

    // Constants used to control the behaviour of the browser process with
    // regards to the shared RELRO section. Not applicable to ModernLinker.
    //   NEVER        -> The browser never uses it itself.
    //   LOW_RAM_ONLY -> It is only used on devices with low RAM.
    //   ALWAYS       -> It is always used.
    // NOTE: These names are known and expected by the Linker test scripts.
    public static final int BROWSER_SHARED_RELRO_CONFIG_NEVER = 0;
    public static final int BROWSER_SHARED_RELRO_CONFIG_LOW_RAM_ONLY = 1;
    public static final int BROWSER_SHARED_RELRO_CONFIG_ALWAYS = 2;

    // Configuration variable used to control how the browser process uses the
    // shared RELRO. Only change this while debugging linker-related issues.
    // Not used by ModernLinker.
    // NOTE: This variable's name is known and expected by the Linker test scripts.
    public static final int BROWSER_SHARED_RELRO_CONFIG =
            BROWSER_SHARED_RELRO_CONFIG_LOW_RAM_ONLY;

    // Constants used to control the memory device config. Can be set explicitly
    // by setMemoryDeviceConfigForTesting(). Not applicable to ModernLinker.
    //   INIT         -> Value is undetermined (will check at runtime).
    //   LOW          -> This is a low-memory device.
    //   NORMAL       -> This is not a low-memory device.
    public static final int MEMORY_DEVICE_CONFIG_INIT = 0;
    public static final int MEMORY_DEVICE_CONFIG_LOW = 1;
    public static final int MEMORY_DEVICE_CONFIG_NORMAL = 2;

    // Indicates if this is a low-memory device or not. The default is to
    // determine this by probing the system at runtime, but this can be forced
    // for testing by calling setMemoryDeviceConfigForTesting().
    // Not used by ModernLinker.
    protected int mMemoryDeviceConfig = MEMORY_DEVICE_CONFIG_INIT;

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
    protected static final int BREAKPAD_GUARD_REGION_BYTES = 16 * 1024 * 1024;

    // Size of the area requested when using ASLR to obtain a random load address.
    // Should match the value of kAddressSpaceReservationSize on the JNI side.
    // Used when computing the load addresses of multiple loaded libraries to
    // ensure that we don't try to load outside the area originally requested.
    protected static final int ADDRESS_SPACE_RESERVATION = 192 * 1024 * 1024;

    // Constants used to indicate a given Linker implementation, for testing.
    //   LEGACY       -> Always uses the LegacyLinker implementation.
    //   MODERN       -> Always uses the ModernLinker implementation.
    // NOTE: These names are known and expected by the Linker test scripts.
    public static final int LINKER_IMPLEMENTATION_LEGACY = 1;
    public static final int LINKER_IMPLEMENTATION_MODERN = 2;

    // Singleton.
    private static Linker sSingleton;
    private static Object sSingletonLock = new Object();

    // Protected singleton constructor.
    protected Linker() { }

    /**
     * Get singleton instance. Returns either a LegacyLinker or a ModernLinker.
     *
     * Returns a ModernLinker if running on Android M or later, otherwise returns
     * a LegacyLinker.
     *
     * ModernLinker requires OS features from Android M and later: a system linker
     * that handles packed relocations and load from APK, and android_dlopen_ext()
     * for shared RELRO support. It cannot run on Android releases earlier than M.
     *
     * LegacyLinker runs on all Android releases but it is slower and more complex
     * than ModernLinker, so ModernLinker is preferred for Android M and later.
     *
     * @return the Linker implementation instance.
     */
    public static final Linker getInstance() {
        synchronized (sSingletonLock) {
            if (sSingleton == null) {
                // With incremental install, it's important to fall back to the "normal"
                // library loading path in order for the libraries to be found.
                String appClass =
                        ContextUtils.getApplicationContext().getApplicationInfo().className;
                boolean isIncrementalInstall =
                        appClass != null && appClass.contains("incrementalinstall");
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !isIncrementalInstall) {
                    sSingleton = ModernLinker.create();
                } else {
                    sSingleton = LegacyLinker.create();
                }
                Log.i(TAG, "Using linker: " + sSingleton.getClass().getName());
            }
            return sSingleton;
        }
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
     * Assert for testing.
     * Hard assertion. Cannot be disabled. Used only by testing methods.
     */
    private static void assertForTesting(boolean flag) {
        if (!flag) {
            throw new AssertionError();
        }
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
        if (!NativeLibraries.sEnableLinkerTests) {
            throw new AssertionError("Testing method called in non-testing context");
        }
    }

    /**
     * Set Linker implementation type.
     * For testing. Sets either a LegacyLinker or a ModernLinker. Must be called
     * before getInstance().
     *
     * @param type LINKER_IMPLEMENTATION_LEGACY or LINKER_IMPLEMENTATION_MODERN
     */
    public static final void setImplementationForTesting(int type) {
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();
        assertForTesting(type == LINKER_IMPLEMENTATION_LEGACY
                         || type == LINKER_IMPLEMENTATION_MODERN);

        synchronized (sSingletonLock) {
            assertForTesting(sSingleton == null);

            if (type == LINKER_IMPLEMENTATION_MODERN) {
                sSingleton = ModernLinker.create();
            } else if (type == LINKER_IMPLEMENTATION_LEGACY) {
                sSingleton = LegacyLinker.create();
            }
            Log.i(TAG, "Forced linker: " + sSingleton.getClass().getName());
        }
    }

    /**
     * Get Linker implementation type.
     * For testing.
     *
     * @return LINKER_IMPLEMENTATION_LEGACY or LINKER_IMPLEMENTATION_MODERN
     */
    public final int getImplementationForTesting() {
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();

        synchronized (sSingletonLock) {
            assertForTesting(sSingleton == this);

            if (sSingleton instanceof ModernLinker) {
                return LINKER_IMPLEMENTATION_MODERN;
            } else if (sSingleton instanceof LegacyLinker) {
                return LINKER_IMPLEMENTATION_LEGACY;
            } else {
                Log.wtf(TAG, "Invalid linker: " + sSingleton.getClass().getName());
                assertForTesting(false);
            }
            return 0;
        }
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
     * Set the TestRunner by its class name. It will be instantiated at
     * runtime after all libraries are loaded.
     *
     * @param testRunnerClassName null or a String for the class name of the
     * TestRunner to use.
     */
    public final void setTestRunnerClassNameForTesting(String testRunnerClassName) {
        if (DEBUG) {
            Log.i(TAG, "setTestRunnerClassNameForTesting(" + testRunnerClassName + ") called");
        }
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();

        synchronized (mLock) {
            assertForTesting(mTestRunnerClassName == null);
            mTestRunnerClassName = testRunnerClassName;
        }
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
     * Set up the Linker for a test.
     * Convenience function that calls setImplementationForTesting() to force an
     * implementation, and then setTestRunnerClassNameForTesting() to set the test
     * class name.
     *
     * On first call, instantiates a Linker of the requested type and sets its test
     * runner class name. On subsequent calls, checks that the singleton produced by
     * the first call matches the requested type and test runner class name.
     */
    public static final void setupForTesting(int type, String testRunnerClassName) {
        if (DEBUG) {
            Log.i(TAG, "setupForTesting(" + type + ", " + testRunnerClassName + ") called");
        }
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();

        synchronized (sSingletonLock) {
            // If this is the first call, configure the Linker to the given type and test class.
            if (sSingleton == null) {
                setImplementationForTesting(type);
                sSingleton.setTestRunnerClassNameForTesting(testRunnerClassName);
                return;
            }

            // If not the first call, check that the Linker configuration matches this request.
            assertForTesting(sSingleton.getImplementationForTesting() == type);
            String ourTestRunnerClassName = sSingleton.getTestRunnerClassNameForTesting();
            if (testRunnerClassName == null) {
                assertForTesting(ourTestRunnerClassName == null);
            } else {
                assertForTesting(ourTestRunnerClassName.equals(testRunnerClassName));
            }
        }
    }

    /**
     * Instantiate and run the current TestRunner, if any. The TestRunner implementation
     * must be instantiated _after_ all libraries are loaded to ensure that its
     * native methods are properly registered.
     *
     * @param memoryDeviceConfig LegacyLinker memory config, or 0 if unused
     * @param inBrowserProcess true if in the browser process
     */
    protected final void runTestRunnerClassForTesting(int memoryDeviceConfig,
                                                      boolean inBrowserProcess) {
        if (DEBUG) {
            Log.i(TAG, "runTestRunnerClassForTesting called");
        }
        // Sanity check. This method may only be called during tests.
        assertLinkerTestsAreEnabled();

        synchronized (mLock) {
            if (mTestRunnerClassName == null) {
                Log.wtf(TAG, "Linker runtime tests not set up for this process");
                assertForTesting(false);
            }
            if (DEBUG) {
                Log.i(TAG, "Instantiating " + mTestRunnerClassName);
            }
            TestRunner testRunner = null;
            try {
                testRunner = (TestRunner) Class.forName(mTestRunnerClassName).newInstance();
            } catch (Exception e) {
                Log.wtf(TAG, "Could not instantiate test runner class by name", e);
                assertForTesting(false);
            }

            if (!testRunner.runChecks(memoryDeviceConfig, inBrowserProcess)) {
                Log.wtf(TAG, "Linker runtime tests failed in this process");
                assertForTesting(false);
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
        assertForTesting(memoryDeviceConfig == MEMORY_DEVICE_CONFIG_LOW
                         || memoryDeviceConfig == MEMORY_DEVICE_CONFIG_NORMAL);

        synchronized (mLock) {
            assertForTesting(mMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_INIT);

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
    public boolean isChromiumLinkerLibrary(String library) {
        return library.equals(LINKER_JNI_LIBRARY);
    }

    /**
     * Load the Linker JNI library. Throws UnsatisfiedLinkError on error.
     */
    protected static void loadLinkerJniLibrary() {
        String libName = "lib" + LINKER_JNI_LIBRARY + ".so";
        if (DEBUG) {
            Log.i(TAG, "Loading " + libName);
        }
        System.loadLibrary(LINKER_JNI_LIBRARY);
    }

    /**
     * Obtain a random base load address at which to place loaded libraries.
     *
     * @return new base load address
     */
    protected long getRandomBaseLoadAddress() {
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
     * Load a native shared library with the Chromium linker. If the zip file
     * is not null, the shared library must be uncompressed and page aligned
     * inside the zipfile. Note the crazy linker treats libraries and files as
     * equivalent, so you can only open one library in a given zip file. The
     * library must not be the Chromium linker library.
     *
     * @param zipFilePath The path of the zip file containing the library (or null).
     * @param libFilePath The path of the library (possibly in the zip file).
     */
    public void loadLibrary(@Nullable String zipFilePath, String libFilePath) {
        if (DEBUG) {
            Log.i(TAG, "loadLibrary: " + zipFilePath + ", " + libFilePath);
        }
        final boolean isFixedAddressPermitted = true;
        loadLibraryImpl(zipFilePath, libFilePath, isFixedAddressPermitted);
    }

    /**
     * Load a native shared library with the Chromium linker, ignoring any
     * requested fixed address for RELRO sharing. If the zip file
     * is not null, the shared library must be uncompressed and page aligned
     * inside the zipfile. Note the crazy linker treats libraries and files as
     * equivalent, so you can only open one library in a given zip file. The
     * library must not be the Chromium linker library.
     *
     * @param zipFilePath The path of the zip file containing the library (or null).
     * @param libFilePath The path of the library (possibly in the zip file).
     */
    public void loadLibraryNoFixedAddress(@Nullable String zipFilePath, String libFilePath) {
        if (DEBUG) {
            Log.i(TAG, "loadLibraryAtAnyAddress: " + zipFilePath + ", " + libFilePath);
        }
        final boolean isFixedAddressPermitted = false;
        loadLibraryImpl(zipFilePath, libFilePath, isFixedAddressPermitted);
    }

    /**
     * Call this method to determine if the chromium project must load the library
     * directly from a zip file.
     */
    public static boolean isInZipFile() {
        // The auto-generated NativeLibraries.sUseLibraryInZipFile variable will be true
        // if the library remains embedded in the APK zip file on the target.
        return NativeLibraries.sUseLibraryInZipFile;
    }

    /**
     * Call this method to determine if this chromium project must
     * use this linker. If not, System.loadLibrary() should be used to load
     * libraries instead.
     */
    public static boolean isUsed() {
        // The auto-generated NativeLibraries.sUseLinker variable will be true if the
        // build has not explicitly disabled Linker features.
        return NativeLibraries.sUseLinker;
    }

    /**
     * Call this method to determine if the linker will try to use shared RELROs
     * for the browser process.
     */
    public abstract boolean isUsingBrowserSharedRelros();

    /**
     * Call this method just before loading any native shared libraries in this process.
     */
    public abstract void prepareLibraryLoad();

    /**
     * Call this method just after loading all native shared libraries in this process.
     */
    public abstract void finishLibraryLoad();

    /**
     * Call this to send a Bundle containing the shared RELRO sections to be
     * used in this process. If initServiceProcess() was previously called,
     * finishLibraryLoad() will not exit until this method is called in another
     * thread with a non-null value.
     *
     * @param bundle The Bundle instance containing a map of shared RELRO sections
     * to use in this process.
     */
    public abstract void useSharedRelros(Bundle bundle);

    /**
     * Call this to retrieve the shared RELRO sections created in this process,
     * after loading all libraries.
     *
     * @return a new Bundle instance, or null if RELRO sharing is disabled on
     * this system, or if initServiceProcess() was called previously.
     */
    public abstract Bundle getSharedRelros();


    /**
     * Call this method before loading any libraries to indicate that this
     * process shall neither create or reuse shared RELRO sections.
     */
    public abstract void disableSharedRelros();

    /**
     * Call this method before loading any libraries to indicate that this
     * process is ready to reuse shared RELRO sections from another one.
     * Typically used when starting service processes.
     *
     * @param baseLoadAddress the base library load address to use.
     */
    public abstract void initServiceProcess(long baseLoadAddress);

    /**
     * Retrieve the base load address of all shared RELRO sections.
     * This also enforces the creation of shared RELRO sections in
     * prepareLibraryLoad(), which can later be retrieved with getSharedRelros().
     *
     * @return a common, random base load address, or 0 if RELRO sharing is
     * disabled.
     */
    public abstract long getBaseLoadAddress();

    /**
     * Implements loading a native shared library with the Chromium linker.
     *
     * @param zipFilePath The path of the zip file containing the library (or null).
     * @param libFilePath The path of the library (possibly in the zip file).
     * @param isFixedAddressPermitted If true, uses a fixed load address if one was
     * supplied, otherwise ignores the fixed address and loads wherever available.
     */
    abstract void loadLibraryImpl(@Nullable String zipFilePath,
                                  String libFilePath,
                                  boolean isFixedAddressPermitted);

    /**
     * Record information for a given library.
     * IMPORTANT: Native code knows about this class's fields, so
     * don't change them without modifying the corresponding C++ sources.
     * Also, the LibInfo instance owns the shared RELRO file descriptor.
     */
    public static class LibInfo implements Parcelable {

        public LibInfo() {
            mLoadAddress = 0;
            mLoadSize = 0;
            mRelroStart = 0;
            mRelroSize = 0;
            mRelroFd = -1;
        }

        public void close() {
            if (mRelroFd >= 0) {
                try {
                    ParcelFileDescriptor.adoptFd(mRelroFd).close();
                } catch (java.io.IOException e) {
                    if (DEBUG) {
                        Log.e(TAG, "Failed to close fd: " + mRelroFd);
                    }
                }
                mRelroFd = -1;
            }
        }

        // from Parcelable
        public LibInfo(Parcel in) {
            mLoadAddress = in.readLong();
            mLoadSize = in.readLong();
            mRelroStart = in.readLong();
            mRelroSize = in.readLong();
            ParcelFileDescriptor fd = ParcelFileDescriptor.CREATOR.createFromParcel(in);
            // If CreateSharedRelro fails, the OS file descriptor will be -1 and |fd| will be null.
            mRelroFd = (fd == null) ? -1 : fd.detachFd();
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

        @Override
        public String toString() {
            return String.format(Locale.US,
                                 "[load=0x%x-0x%x relro=0x%x-0x%x fd=%d]",
                                 mLoadAddress,
                                 mLoadAddress + mLoadSize,
                                 mRelroStart,
                                 mRelroStart + mRelroSize,
                                 mRelroFd);
        }

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
        public int  mRelroFd;     // shared RELRO file descriptor, or -1
    }

    // Create a Bundle from a map of LibInfo objects.
    protected Bundle createBundleFromLibInfoMap(HashMap<String, LibInfo> map) {
        Bundle bundle = new Bundle(map.size());
        for (Map.Entry<String, LibInfo> entry : map.entrySet()) {
            bundle.putParcelable(entry.getKey(), entry.getValue());
        }
        return bundle;
    }

    // Create a new LibInfo map from a Bundle.
    protected HashMap<String, LibInfo> createLibInfoMapFromBundle(Bundle bundle) {
        HashMap<String, LibInfo> map = new HashMap<String, LibInfo>();
        for (String library : bundle.keySet()) {
            LibInfo libInfo = bundle.getParcelable(library);
            map.put(library, libInfo);
        }
        return map;
    }

    // Call the close() method on all values of a LibInfo map.
    protected void closeLibInfoMap(HashMap<String, LibInfo> map) {
        for (Map.Entry<String, LibInfo> entry : map.entrySet()) {
            entry.getValue().close();
        }
    }

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
