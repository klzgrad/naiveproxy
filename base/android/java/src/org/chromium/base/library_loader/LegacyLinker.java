// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.library_loader;

import android.os.Bundle;
import android.os.Parcel;

import org.chromium.base.Log;
import org.chromium.base.SysUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.MainDex;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

import javax.annotation.Nullable;

/*
 * For more, see Technical note, Security considerations, and the explanation
 * of how this class is supposed to be used in Linker.java.
 */

/**
 * Provides a concrete implementation of the Chromium Linker.
 *
 * This Linker implementation uses the crazy linker to map and then run Chrome
 * for Android.
 *
 * For more on the operations performed by the Linker, see {@link Linker}.
 */
@MainDex
class LegacyLinker extends Linker {
    // Log tag for this class.
    private static final String TAG = "LibraryLoader";

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

    // Private singleton constructor, and singleton factory method.
    private LegacyLinker() { }
    static Linker create() {
        return new LegacyLinker();
    }

    // Used internally to initialize the linker's data. Assumes lock is held.
    // Loads JNI, and sets mMemoryDeviceConfig and mBrowserUsesSharedRelro.
    private void ensureInitializedLocked() {
        assert Thread.holdsLock(mLock);

        if (mInitialized || !NativeLibraries.sUseLinker) {
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
    @Override
    public boolean isUsingBrowserSharedRelros() {
        synchronized (mLock) {
            ensureInitializedLocked();
            return mInBrowserProcess && mBrowserUsesSharedRelro;
        }
    }

    /**
     * Call this method just before loading any native shared libraries in this process.
     */
    @Override
    public void prepareLibraryLoad() {
        if (DEBUG) {
            Log.i(TAG, "prepareLibraryLoad() called");
        }
        synchronized (mLock) {
            ensureInitializedLocked();
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
    @Override
    public void finishLibraryLoad() {
        if (DEBUG) {
            Log.i(TAG, "finishLibraryLoad() called");
        }
        synchronized (mLock) {
            ensureInitializedLocked();
            if (DEBUG) {
                Log.i(TAG, String.format(
                        Locale.US,
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
    @Override
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
            Log.i(TAG, "useSharedRelros() called with " + bundle
                    + ", cloned " + clonedBundle);
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
    @Override
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
    @Override
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
    @Override
    public void initServiceProcess(long baseLoadAddress) {
        if (DEBUG) {
            Log.i(TAG, String.format(
                    Locale.US, "initServiceProcess(0x%x) called",
                    baseLoadAddress));
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
    @Override
    public long getBaseLoadAddress() {
        synchronized (mLock) {
            ensureInitializedLocked();
            if (!mInBrowserProcess) {
                Log.w(TAG, "Shared RELRO sections are disabled in this process!");
                return 0;
            }

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

            String sharedRelRoName = libFilePath;
            if (zipFilePath != null) {
                if (!nativeLoadLibraryInZipFile(zipFilePath, libFilePath, loadAddress, libInfo)) {
                    String errorMessage = "Unable to load library: " + libFilePath
                                          + ", in: " + zipFilePath;
                    Log.e(TAG, errorMessage);
                    throw new UnsatisfiedLinkError(errorMessage);
                }
                sharedRelRoName = zipFilePath;
            } else {
                if (!nativeLoadLibrary(libFilePath, loadAddress, libInfo)) {
                    String errorMessage = "Unable to load library: " + libFilePath;
                    Log.e(TAG, errorMessage);
                    throw new UnsatisfiedLinkError(errorMessage);
                }
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

            if (mInBrowserProcess) {
                // Create a new shared RELRO section at the 'current' fixed load address.
                if (!nativeCreateSharedRelro(sharedRelRoName, mCurrentLoadAddress, libInfo)) {
                    Log.w(TAG, String.format(
                            Locale.US, "Could not create shared RELRO for %s at %x",
                            libFilePath,
                            mCurrentLoadAddress));
                } else {
                    if (DEBUG) {
                        Log.i(TAG, String.format(
                                Locale.US,
                                "Created shared RELRO for %s at %x: %s",
                                sharedRelRoName,
                                mCurrentLoadAddress,
                                libInfo.toString()));
                    }
                }
            }

            if (loadAddress != 0 && mCurrentLoadAddress != 0) {
                // Compute the next current load address. If mCurrentLoadAddress
                // is not 0, this is an explicit library load address. Otherwise,
                // this is an explicit load address for relocated RELRO sections
                // only.
                mCurrentLoadAddress = libInfo.mLoadAddress + libInfo.mLoadSize
                                      + BREAKPAD_GUARD_REGION_BYTES;
            }

            mLoadedLibraries.put(sharedRelRoName, libInfo);
            if (DEBUG) {
                Log.i(TAG, "Library details " + libInfo.toString());
            }
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
    public static void postCallbackOnMainThread(final long opaque) {
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
    private static native boolean nativeLoadLibrary(String library,
                                                    long loadAddress,
                                                    LibInfo libInfo);

    /**
     * Native method used to load a library which is inside a zipfile.
     *
     * @param zipfileName Filename of the zip file containing the library.
     * @param library Platform specific library name (e.g. libfoo.so)
     * @param loadAddress Explicit load address, or 0 for randomized one.
     * @param libInfo If not null, the mLoadAddress and mLoadSize fields
     * of this LibInfo instance will set on success.
     * @return true for success, false otherwise.
     */
    private static native boolean nativeLoadLibraryInZipFile(@Nullable String zipfileName,
                                                             String libraryName,
                                                             long loadAddress,
                                                             LibInfo libInfo);

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
    private static native boolean nativeCreateSharedRelro(String library,
                                                          long loadAddress,
                                                          LibInfo libInfo);

    /**
     * Native method used to use a shared RELRO section.
     *
     * @param library Library name.
     * @param libInfo A LibInfo instance containing valid RELRO information
     * @return true on success.
     */
    private static native boolean nativeUseSharedRelro(String library,
                                                       LibInfo libInfo);
}
