// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.library_loader;

import android.os.Bundle;
import android.os.SystemClock;

import org.chromium.base.Log;
import org.chromium.base.PathUtils;
import org.chromium.base.annotations.SuppressFBWarnings;

import java.util.HashMap;
import java.util.Locale;

import javax.annotation.Nullable;

/*
 * For more, see Technical note, Security considerations, and the explanation
 * of how this class is supposed to be used in Linker.java.
 */

/**
 * Provides a concrete implementation of the Chromium Linker.
 *
 * This Linker implementation uses the Android M and later system linker to map and then
 * run Chrome for Android.
 *
 * For more on the operations performed by the Linker, see {@link Linker}.
 */
class ModernLinker extends Linker {
    // Log tag for this class.
    private static final String TAG = "LibraryLoader";

    // Becomes true after linker initialization.
    private boolean mInitialized;

    // Becomes true to indicate this process needs to wait for a shared RELRO in LibraryLoad().
    private boolean mWaitForSharedRelros;

    // The map of all RELRO sections either created or used in this process.
    private HashMap<String, LibInfo> mSharedRelros;

    // Cached Bundle representation of the RELRO sections map for transfer across processes.
    private Bundle mSharedRelrosBundle;

    // Set to true if this runs in the browser process. Disabled by initServiceProcess().
    private boolean mInBrowserProcess = true;

    // Current common random base load address. A value of -1 indicates not yet initialized.
    private long mBaseLoadAddress = -1;

    // Current fixed-location load address for the next library called by loadLibrary().
    // Initialized to mBaseLoadAddress in prepareLibraryLoad(), and then adjusted as each
    // library is loaded by loadLibrary().
    private long mCurrentLoadAddress = -1;

    // Becomes true once prepareLibraryLoad() has been called.
    private boolean mPrepareLibraryLoadCalled;

    // The map of libraries that are currently loaded in this process.
    private HashMap<String, LibInfo> mLoadedLibraries;

    // Private singleton constructor, and singleton factory method.
    private ModernLinker() { }
    static Linker create() {
        return new ModernLinker();
    }

    // Used internally to initialize the linker's data. Assumes lock is held.
    private void ensureInitializedLocked() {
        assert Thread.holdsLock(mLock);
        assert NativeLibraries.sUseLinker;

        // On first call, load libchromium_android_linker.so. Cannot be done in the
        // constructor because the instance is constructed on the UI thread.
        if (!mInitialized) {
            loadLinkerJniLibrary();
            mInitialized = true;
        }
    }

    /**
     * Call this method to determine if the linker will try to use shared RELROs
     * for the browser process.
     */
    @Override
    public boolean isUsingBrowserSharedRelros() {
        // This Linker does not attempt to share RELROS between the browser and
        // the renderers, but only between renderers.
        return false;
    }

    /**
     * Call this method just before loading any native shared libraries in this process.
     * Loads the Linker's JNI library, and initializes the variables involved in the
     * implementation of shared RELROs.
     */
    @Override
    public void prepareLibraryLoad() {
        if (DEBUG) {
            Log.i(TAG, "prepareLibraryLoad() called");
        }
        assert NativeLibraries.sUseLinker;

        synchronized (mLock) {
            assert !mPrepareLibraryLoadCalled;
            ensureInitializedLocked();

            // If in the browser, generate a random base load address for service processes
            // and create an empty shared RELROs map. For service processes, the shared
            // RELROs map must remain null until set by useSharedRelros().
            if (mInBrowserProcess) {
                setupBaseLoadAddressLocked();
                mSharedRelros = new HashMap<String, LibInfo>();
            }

            // Create an empty loaded libraries map.
            mLoadedLibraries = new HashMap<String, LibInfo>();

            // Start the current load address at the base load address.
            mCurrentLoadAddress = mBaseLoadAddress;

            mPrepareLibraryLoadCalled = true;
        }
    }

    /**
     * Call this method just after loading all native shared libraries in this process.
     * If not in the browser, closes the LibInfo entries used for RELRO sharing.
     */
    @Override
    public void finishLibraryLoad() {
        if (DEBUG) {
            Log.i(TAG, "finishLibraryLoad() called");
        }

        synchronized (mLock) {
            assert mPrepareLibraryLoadCalled;

            // Close shared RELRO file descriptors if not in the browser.
            if (!mInBrowserProcess && mSharedRelros != null) {
                closeLibInfoMap(mSharedRelros);
                mSharedRelros = null;
            }

            // If testing, run tests now that all libraries are loaded and initialized.
            if (NativeLibraries.sEnableLinkerTests) {
                runTestRunnerClassForTesting(0, mInBrowserProcess);
            }
        }
    }

    // Used internally to wait for shared RELROs. Returns once useSharedRelros() has been
    // called to supply a valid shared RELROs bundle.
    @SuppressFBWarnings("RCN_REDUNDANT_NULLCHECK_OF_NULL_VALUE")
    private void waitForSharedRelrosLocked() {
        if (DEBUG) {
            Log.i(TAG, "waitForSharedRelros called");
        }
        assert Thread.holdsLock(mLock);

        // Return immediately if shared RELROs are already available.
        if (mSharedRelros != null) {
            return;
        }

        // Wait until notified by useSharedRelros() that shared RELROs have arrived.
        long startTime = DEBUG ? SystemClock.uptimeMillis() : 0;
        while (mSharedRelros == null) {
            try {
                mLock.wait();
            } catch (InterruptedException e) {
                // Restore the thread's interrupt status.
                Thread.currentThread().interrupt();
            }
        }

        if (DEBUG) {
            Log.i(TAG, String.format(
                    Locale.US, "Time to wait for shared RELRO: %d ms",
                    SystemClock.uptimeMillis() - startTime));
        }
    }

    /**
     * Call this to send a Bundle containing the shared RELRO sections to be
     * used in this process. If initServiceProcess() was previously called,
     * libraryLoad() will wait until this method is called in another
     * thread with a non-null value.
     *
     * @param bundle The Bundle instance containing a map of shared RELRO sections
     * to use in this process.
     */
    @Override
    public void useSharedRelros(Bundle bundle) {
        if (DEBUG) {
            Log.i(TAG, "useSharedRelros() called with " + bundle);
        }

        synchronized (mLock) {
            mSharedRelros = createLibInfoMapFromBundle(bundle);
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
    @Override
    public Bundle getSharedRelros() {
        if (DEBUG) {
            Log.i(TAG, "getSharedRelros() called");
        }
        synchronized (mLock) {
            if (!mInBrowserProcess) {
                if (DEBUG) {
                    Log.i(TAG, "Not in browser, so returning null Bundle");
                }
                return null;
            }

            // Create a new Bundle containing RELRO section information for all the shared
            // RELROs created while loading libraries.
            if (mSharedRelrosBundle == null && mSharedRelros != null) {
                mSharedRelrosBundle = createBundleFromLibInfoMap(mSharedRelros);
                if (DEBUG) {
                    Log.i(TAG, "Shared RELRO bundle created from map: " + mSharedRelrosBundle);
                }
            }
            if (DEBUG) {
                Log.i(TAG, "Returning " + mSharedRelrosBundle);
            }
            return mSharedRelrosBundle;
        }
    }


    /**
     * Call this method before loading any libraries to indicate that this
     * process shall neither create or reuse shared RELRO sections.
     */
    @Override
    public void disableSharedRelros() {
        if (DEBUG) {
            Log.i(TAG, "disableSharedRelros() called");
        }
        synchronized (mLock) {
            // Mark this as a service process, and disable wait for shared RELRO.
            mInBrowserProcess = false;
            mWaitForSharedRelros = false;
        }
    }

    /**
     * Call this method before loading any libraries to indicate that this
     * process is ready to reuse shared RELRO sections from another one.
     * Typically used when starting service processes.
     *
     * @param baseLoadAddress the base library load address to use.
     */
    @Override
    public void initServiceProcess(long baseLoadAddress) {
        if (DEBUG) {
            Log.i(TAG, String.format(
                    Locale.US, "initServiceProcess(0x%x) called",
                    baseLoadAddress));
        }
        synchronized (mLock) {
            assert !mPrepareLibraryLoadCalled;

            // Mark this as a service process, and flag wait for shared RELRO.
            // Save the base load address passed in.
            mInBrowserProcess = false;
            mWaitForSharedRelros = true;
            mBaseLoadAddress = baseLoadAddress;
        }
    }

    /**
     * Retrieve the base load address for libraries that share RELROs.
     *
     * @return a common, random base load address, or 0 if RELRO sharing is
     * disabled.
     */
    @Override
    public long getBaseLoadAddress() {
        synchronized (mLock) {
            ensureInitializedLocked();
            setupBaseLoadAddressLocked();
            if (DEBUG) {
                Log.i(TAG, String.format(
                        Locale.US, "getBaseLoadAddress() returns 0x%x",
                        mBaseLoadAddress));
            }
            return mBaseLoadAddress;
        }
    }

    // Used internally to lazily setup the common random base load address.
    private void setupBaseLoadAddressLocked() {
        assert Thread.holdsLock(mLock);

        // No-op if the base load address is already set up.
        if (mBaseLoadAddress == -1) {
            mBaseLoadAddress = getRandomBaseLoadAddress();
        }
        if (mBaseLoadAddress == 0) {
            // If the random address is 0 there are issues with finding enough
            // free address space, so disable RELRO shared / fixed load addresses.
            Log.w(TAG, "Disabling shared RELROs due address space pressure");
            mWaitForSharedRelros = false;
        }
    }

    /**
     * Load a native shared library with the Chromium linker. If the zip file
     * is not null, the shared library must be uncompressed and page aligned
     * inside the zipfile. The library must not be the Chromium linker library.
     *
     * If asked to wait for shared RELROs, this function will block library loads
     * until the shared RELROs bundle is received by useSharedRelros().
     *
     * @param zipFilePath The path of the zip file containing the library (or null).
     * @param libFilePath The path of the library (possibly in the zip file).
     * @param isFixedAddressPermitted If true, uses a fixed load address if one was
     * supplied, otherwise ignores the fixed address and loads wherever available.
     */
    @Override
    void loadLibraryImpl(@Nullable String zipFilePath,
                         String libFilePath,
                         boolean isFixedAddressPermitted) {
        if (DEBUG) {
            Log.i(TAG, "loadLibraryImpl: "
                    + zipFilePath + ", " + libFilePath + ", " + isFixedAddressPermitted);
        }

        synchronized (mLock) {
            assert mPrepareLibraryLoadCalled;

            String dlopenExtPath;
            if (zipFilePath != null) {
                // The android_dlopen_ext() function understands strings with the format
                // <zip_path>!/<file_path> to represent the file_path element within the zip
                // file at zip_path. This enables directly loading from APK. We add the
                // "crazy." prefix to the path in the zip file to prevent the Android package
                // manager from seeing this as a library and so extracting it from the APK.
                String cpuAbi = nativeGetCpuAbi();
                dlopenExtPath = zipFilePath + "!/lib/" + cpuAbi + "/crazy." + libFilePath;
            } else {
                // Not loading from APK directly, so simply pass the library name to
                // android_dlopen_ext().
                dlopenExtPath = libFilePath;
            }

            if (mLoadedLibraries.containsKey(dlopenExtPath)) {
                if (DEBUG) {
                    Log.i(TAG, "Not loading " + libFilePath + " twice");
                }
                return;
            }

            // If not in the browser, shared RELROs are not disabled, and fixed addresses
            // have not been disallowed, load the library at a fixed address. Otherwise,
            // load anywhere.
            long loadAddress = 0;
            if (!mInBrowserProcess && mWaitForSharedRelros && isFixedAddressPermitted) {
                loadAddress = mCurrentLoadAddress;

                // For multiple libraries, ensure we stay within reservation range.
                if (loadAddress > mBaseLoadAddress + ADDRESS_SPACE_RESERVATION) {
                    String errorMessage = "Load address outside reservation, for: " + libFilePath;
                    Log.e(TAG, errorMessage);
                    throw new UnsatisfiedLinkError(errorMessage);
                }
            }

            LibInfo libInfo = new LibInfo();

            if (mInBrowserProcess && mCurrentLoadAddress != 0) {
                // We are in the browser, and with a current load address that indicates that
                // there is enough address space for shared RELRO to operate. Create the
                // shared RELRO, and store it in the map.
                String relroPath = PathUtils.getDataDirectory() + "/RELRO:" + libFilePath;
                if (nativeCreateSharedRelro(dlopenExtPath,
                                            mCurrentLoadAddress, relroPath, libInfo)) {
                    mSharedRelros.put(dlopenExtPath, libInfo);
                } else {
                    String errorMessage = "Unable to create shared relro: " + relroPath;
                    Log.w(TAG, errorMessage);
                }
            } else if (!mInBrowserProcess && mCurrentLoadAddress != 0 && mWaitForSharedRelros) {
                // We are in a service process, again with a current load address that is
                // suitable for shared RELRO, and we are to wait for shared RELROs. So
                // do that, then use the map we receive to provide libinfo for library load.
                waitForSharedRelrosLocked();
                if (mSharedRelros.containsKey(dlopenExtPath)) {
                    libInfo = mSharedRelros.get(dlopenExtPath);
                }
            }

            // Load the library. In the browser, loadAddress is 0, so nativeLoadLibrary()
            // will load without shared RELRO. Otherwise, it uses shared RELRO if the attached
            // libInfo is usable.
            if (!nativeLoadLibrary(dlopenExtPath, loadAddress, libInfo)) {
                String errorMessage = "Unable to load library: " + dlopenExtPath;
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
                String tag = mInBrowserProcess ? "BROWSER_LIBRARY_ADDRESS"
                                               : "RENDERER_LIBRARY_ADDRESS";
                Log.i(TAG, String.format(
                        Locale.US, "%s: %s %x", tag, libFilePath, libInfo.mLoadAddress));
            }

            if (loadAddress != 0 && mCurrentLoadAddress != 0) {
                // Compute the next current load address. If mCurrentLoadAddress
                // is not 0, this is an explicit library load address.
                mCurrentLoadAddress = libInfo.mLoadAddress + libInfo.mLoadSize
                                      + BREAKPAD_GUARD_REGION_BYTES;
            }

            mLoadedLibraries.put(dlopenExtPath, libInfo);
            if (DEBUG) {
                Log.i(TAG, "Library details " + libInfo.toString());
            }
        }
    }

    /**
     * Native method to return the CPU ABI.
     * Obtaining it from the linker's native code means that we always correctly
     * match the loaded library's ABI to the linker's ABI.
     *
     * @return CPU ABI string.
     */
    private static native String nativeGetCpuAbi();

    /**
     * Native method used to load a library.
     *
     * @param dlopenExtPath For load from APK, the path to the enclosing
     * zipfile concatenated with "!/" and the path to the library within the zipfile;
     * otherwise the platform specific library name (e.g. libfoo.so).
     * @param loadAddress Explicit load address, or 0 for randomized one.
     * @param libInfo If not null, the mLoadAddress and mLoadSize fields
     * of this LibInfo instance will set on success.
     * @return true for success, false otherwise.
     */
    private static native boolean nativeLoadLibrary(String dlopenExtPath,
                                                    long loadAddress,
                                                    LibInfo libInfo);

    /**
     * Native method used to create a shared RELRO section.
     * Creates a shared RELRO file for the given library. Done by loading a
     * a new temporary library at the specified address, saving the RELRO section
     * from it, then unloading.
     *
     * @param dlopenExtPath For load from APK, the path to the enclosing
     * zipfile concatenated with "!/" and the path to the library within the zipfile;
     * otherwise the platform specific library name (e.g. libfoo.so).
     * @param loadAddress load address, which can be different from the one
     * used to load the library in the current process!
     * @param relroPath Path to the shared RELRO file for this library.
     * @param libInfo libInfo instance. On success, the mRelroStart, mRelroSize
     * and mRelroFd will be set.
     * @return true on success, false otherwise.
     */
    private static native boolean nativeCreateSharedRelro(String dlopenExtPath,
                                                          long loadAddress,
                                                          String relroPath,
                                                          LibInfo libInfo);
}
