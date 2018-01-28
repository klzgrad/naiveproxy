// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import android.content.Context;
import android.content.Intent;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.Process;
import android.os.RemoteException;
import android.util.SparseArray;

import org.chromium.base.BaseSwitches;
import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.annotations.SuppressFBWarnings;

import java.util.List;
import java.util.concurrent.Semaphore;

import javax.annotation.concurrent.GuardedBy;

/**
 * This class implements all of the functionality for {@link ChildProcessService} which owns an
 * object of {@link ChildProcessServiceImpl}.
 * It makes it possible for other consumer services (such as WebAPKs) to reuse that logic.
 */
@JNINamespace("base::android")
@MainDex
public class ChildProcessServiceImpl {
    private static final String MAIN_THREAD_NAME = "ChildProcessMain";
    private static final String TAG = "ChildProcessService";

    // Only for a check that create is only called once.
    private static boolean sCreateCalled;

    private final ChildProcessServiceDelegate mDelegate;

    private final Object mBinderLock = new Object();
    private final Object mLibraryInitializedLock = new Object();

    // True if we should enforce that bindToCaller() is called before setupConnection().
    // Only set once in bind(), does not require synchronization.
    private boolean mBindToCallerCheck;

    // PID of the client of this service, set in bindToCaller(), if mBindToCallerCheck is true.
    @GuardedBy("mBinderLock")
    private int mBoundCallingPid;

    // This is the native "Main" thread for the renderer / utility process.
    private Thread mMainThread;

    // Parameters received via IPC, only accessed while holding the mMainThread monitor.
    private String[] mCommandLineParams;

    // File descriptors that should be registered natively.
    private FileDescriptorInfo[] mFdInfos;

    @GuardedBy("mLibraryInitializedLock")
    private boolean mLibraryInitialized;

    // Called once the service is bound and all service related member variables have been set.
    // Only set once in bind(), does not require synchronization.
    private boolean mServiceBound;

    /**
     * If >= 0 enables "validation of caller of {@link mBinder}'s methods". A RemoteException
     * is thrown when an application with a uid other than {@link mAuthorizedCallerUid} calls
     * {@link mBinder}'s methods.
     * Only set once in {@link bind}, does not require synchronization.
     */
    private int mAuthorizedCallerUid;

    private final Semaphore mActivitySemaphore = new Semaphore(1);

    public ChildProcessServiceImpl(ChildProcessServiceDelegate delegate) {
        mDelegate = delegate;
    }

    // Binder object used by clients for this service.
    private final IChildProcessService.Stub mBinder = new IChildProcessService.Stub() {
        // NOTE: Implement any IChildProcessService methods here.
        @Override
        public boolean bindToCaller() {
            assert mBindToCallerCheck;
            assert mServiceBound;
            synchronized (mBinderLock) {
                int callingPid = Binder.getCallingPid();
                if (mBoundCallingPid == 0) {
                    mBoundCallingPid = callingPid;
                } else if (mBoundCallingPid != callingPid) {
                    Log.e(TAG, "Service is already bound by pid %d, cannot bind for pid %d",
                            mBoundCallingPid, callingPid);
                    return false;
                }
            }
            return true;
        }

        @Override
        public void setupConnection(Bundle args, ICallbackInt pidCallback, List<IBinder> callbacks)
                throws RemoteException {
            assert mServiceBound;
            synchronized (mBinderLock) {
                if (mBindToCallerCheck && mBoundCallingPid == 0) {
                    Log.e(TAG, "Service has not been bound with bindToCaller()");
                    pidCallback.call(-1);
                    return;
                }
            }

            pidCallback.call(Process.myPid());
            processConnectionBundle(args, callbacks);
        }

        @Override
        public void crashIntentionallyForTesting() {
            assert mServiceBound;
            Process.killProcess(Process.myPid());
        }

        @Override
        public boolean onTransact(int arg0, Parcel arg1, Parcel arg2, int arg3)
                throws RemoteException {
            assert mServiceBound;
            if (mAuthorizedCallerUid >= 0) {
                int callingUid = Binder.getCallingUid();
                if (callingUid != mAuthorizedCallerUid) {
                    throw new RemoteException("Unauthorized caller " + callingUid
                            + "does not match expected host=" + mAuthorizedCallerUid);
                }
            }
            return super.onTransact(arg0, arg1, arg2, arg3);
        }
    };

    // The ClassLoader for the host context.
    private ClassLoader mHostClassLoader;

    /**
     * Loads Chrome's native libraries and initializes a ChildProcessServiceImpl.
     * @param context The application context.
     * @param hostContext The host context the library should be loaded with (i.e. Chrome).
     */
    @SuppressFBWarnings("ST_WRITE_TO_STATIC_FROM_INSTANCE_METHOD") // For sCreateCalled check.
    public void create(final Context context, final Context hostContext) {
        mHostClassLoader = hostContext.getClassLoader();
        Log.i(TAG, "Creating new ChildProcessService pid=%d", Process.myPid());
        if (sCreateCalled) {
            throw new RuntimeException("Illegal child process reuse.");
        }
        sCreateCalled = true;

        // Initialize the context for the application that owns this ChildProcessServiceImpl object.
        ContextUtils.initApplicationContext(context);

        mDelegate.onServiceCreated();

        mMainThread = new Thread(new Runnable() {
            @Override
            @SuppressFBWarnings("DM_EXIT")
            public void run() {
                try {
                    // CommandLine must be initialized before everything else.
                    synchronized (mMainThread) {
                        while (mCommandLineParams == null) {
                            mMainThread.wait();
                        }
                    }
                    assert mServiceBound;
                    CommandLine.init(mCommandLineParams);

                    if (CommandLine.getInstance().hasSwitch(
                                BaseSwitches.RENDERER_WAIT_FOR_JAVA_DEBUGGER)) {
                        android.os.Debug.waitForDebugger();
                    }

                    boolean nativeLibraryLoaded = false;
                    try {
                        nativeLibraryLoaded = mDelegate.loadNativeLibrary(hostContext);
                    } catch (Exception e) {
                        Log.e(TAG, "Failed to load native library.", e);
                    }
                    if (!nativeLibraryLoaded) {
                        System.exit(-1);
                    }

                    synchronized (mLibraryInitializedLock) {
                        mLibraryInitialized = true;
                        mLibraryInitializedLock.notifyAll();
                    }
                    synchronized (mMainThread) {
                        mMainThread.notifyAll();
                        while (mFdInfos == null) {
                            mMainThread.wait();
                        }
                    }

                    SparseArray<String> idsToKeys = mDelegate.getFileDescriptorsIdsToKeys();

                    int[] fileIds = new int[mFdInfos.length];
                    String[] keys = new String[mFdInfos.length];
                    int[] fds = new int[mFdInfos.length];
                    long[] regionOffsets = new long[mFdInfos.length];
                    long[] regionSizes = new long[mFdInfos.length];
                    for (int i = 0; i < mFdInfos.length; i++) {
                        FileDescriptorInfo fdInfo = mFdInfos[i];
                        String key = idsToKeys != null ? idsToKeys.get(fdInfo.id) : null;
                        if (key != null) {
                            keys[i] = key;
                        } else {
                            fileIds[i] = fdInfo.id;
                        }
                        fds[i] = fdInfo.fd.detachFd();
                        regionOffsets[i] = fdInfo.offset;
                        regionSizes[i] = fdInfo.size;
                    }
                    nativeRegisterFileDescriptors(keys, fileIds, fds, regionOffsets, regionSizes);

                    mDelegate.onBeforeMain();
                    if (mActivitySemaphore.tryAcquire()) {
                        mDelegate.runMain();
                        nativeExitChildProcess();
                    }
                } catch (InterruptedException e) {
                    Log.w(TAG, "%s startup failed: %s", MAIN_THREAD_NAME, e);
                }
            }
        }, MAIN_THREAD_NAME);
        mMainThread.start();
    }

    @SuppressFBWarnings("DM_EXIT")
    public void destroy() {
        Log.i(TAG, "Destroying ChildProcessService pid=%d", Process.myPid());
        if (mActivitySemaphore.tryAcquire()) {
            // TODO(crbug.com/457406): This is a bit hacky, but there is no known better solution
            // as this service will get reused (at least if not sandboxed).
            // In fact, we might really want to always exit() from onDestroy(), not just from
            // the early return here.
            System.exit(0);
            return;
        }
        synchronized (mLibraryInitializedLock) {
            try {
                while (!mLibraryInitialized) {
                    // Avoid a potential race in calling through to native code before the library
                    // has loaded.
                    mLibraryInitializedLock.wait();
                }
            } catch (InterruptedException e) {
                // Ignore
            }
        }
        mDelegate.onDestroy();
    }

    /*
     * Returns the communication channel to the service. Note that even if multiple clients were to
     * connect, we should only get one call to this method. So there is no need to synchronize
     * member variables that are only set in this method and accessed from binder methods, as binder
     * methods can't be called until this method returns.
     * @param intent The intent that was used to bind to the service.
     * @param authorizedCallerUid If >= 0, enables "validation of service caller". A RemoteException
     * is thrown when an application with a uid other than {@link authorizedCallerUid} calls the
     * service's methods.
     * @return the binder used by the client to setup the connection.
     */
    public IBinder bind(Intent intent, int authorizedCallerUid) {
        assert !mServiceBound;
        mAuthorizedCallerUid = authorizedCallerUid;
        mBindToCallerCheck =
                intent.getBooleanExtra(ChildProcessConstants.EXTRA_BIND_TO_CALLER, false);
        mServiceBound = true;
        mDelegate.onServiceBound(intent);
        return mBinder;
    }

    private void processConnectionBundle(Bundle bundle, List<IBinder> clientInterfaces) {
        // Required to unparcel FileDescriptorInfo.
        bundle.setClassLoader(mHostClassLoader);
        synchronized (mMainThread) {
            if (mCommandLineParams == null) {
                mCommandLineParams =
                        bundle.getStringArray(ChildProcessConstants.EXTRA_COMMAND_LINE);
                mMainThread.notifyAll();
            }
            // We must have received the command line by now
            assert mCommandLineParams != null;
            Parcelable[] fdInfosAsParcelable =
                    bundle.getParcelableArray(ChildProcessConstants.EXTRA_FILES);
            if (fdInfosAsParcelable != null) {
                // For why this arraycopy is necessary:
                // http://stackoverflow.com/questions/8745893/i-dont-get-why-this-classcastexception-occurs
                mFdInfos = new FileDescriptorInfo[fdInfosAsParcelable.length];
                System.arraycopy(fdInfosAsParcelable, 0, mFdInfos, 0, fdInfosAsParcelable.length);
            }
            mDelegate.onConnectionSetup(bundle, clientInterfaces);
            mMainThread.notifyAll();
        }
    }

    /**
     * Helper for registering FileDescriptorInfo objects with GlobalFileDescriptors or
     * FileDescriptorStore.
     * This includes the IPC channel, the crash dump signals and resource related
     * files.
     */
    private static native void nativeRegisterFileDescriptors(
            String[] keys, int[] id, int[] fd, long[] offset, long[] size);

    /**
     * Force the child process to exit.
     */
    private static native void nativeExitChildProcess();
}
