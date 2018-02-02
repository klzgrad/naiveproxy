// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.MessageQueue;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.lang.Thread.UncaughtExceptionHandler;

/**
 * Thread in Java with an Android Handler. This class is not thread safe.
 */
@JNINamespace("base::android")
public class JavaHandlerThread {
    private final HandlerThread mThread;

    private Throwable mUnhandledException;

    /**
     * Construct a java-only instance. Can be connected with native side later.
     * Useful for cases where a java thread is needed before native library is loaded.
     */
    public JavaHandlerThread(String name) {
        mThread = new HandlerThread(name);
    }

    @CalledByNative
    private static JavaHandlerThread create(String name) {
        return new JavaHandlerThread(name);
    }

    public Looper getLooper() {
        assert hasStarted();
        return mThread.getLooper();
    }

    public void maybeStart() {
        if (hasStarted()) return;
        mThread.start();
    }

    @CalledByNative
    private void startAndInitialize(final long nativeThread, final long nativeEvent) {
        maybeStart();
        new Handler(mThread.getLooper()).post(new Runnable() {
            @Override
            public void run() {
                nativeInitializeThread(nativeThread, nativeEvent);
            }
        });
    }

    @CalledByNative
    private void stopOnThread(final long nativeThread) {
        nativeStopThread(nativeThread);
        MessageQueue queue = Looper.myQueue();
        // Add an idle handler so that the thread cleanup code can run after the message loop has
        // detected an idle state and quit properly.
        // This matches the behavior of base::Thread in that it will keep running non-delayed posted
        // tasks indefinitely (until an idle state is reached). HandlerThread#quit() and
        // HandlerThread#quitSafely() aren't sufficient because they prevent new tasks from being
        // added to the queue, and don't allow us to wait for the Runloop to quit properly before
        // stopping the thread.
        queue.addIdleHandler(new MessageQueue.IdleHandler() {
            @Override
            public boolean queueIdle() {
                // The MessageQueue may not be empty, but only delayed tasks remain. To
                // match the behavior of other platforms, we should quit now. Calling quit
                // here is equivalent to calling quitSafely(), but doesn't require target
                // API guards.
                mThread.getLooper().quit();
                nativeOnLooperStopped(nativeThread);
                return false;
            }
        });
    }

    @CalledByNative
    private void joinThread() {
        boolean joined = false;
        while (!joined) {
            try {
                mThread.join();
                joined = true;
            } catch (InterruptedException e) {
            }
        }
    }

    @CalledByNative
    private void stop(final long nativeThread) {
        assert hasStarted();
        // Looper may be null if the thread crashed.
        Looper looper = mThread.getLooper();
        if (!isAlive() || looper == null) return;
        new Handler(looper).post(new Runnable() {
            @Override
            public void run() {
                stopOnThread(nativeThread);
            }
        });
        joinThread();
    }

    private boolean hasStarted() {
        return mThread.getState() != Thread.State.NEW;
    }

    @CalledByNative
    private boolean isAlive() {
        return mThread.isAlive();
    }

    // This should *only* be used for tests. In production we always need to call the original
    // uncaught exception handler (the framework's) after any uncaught exception handling we do, as
    // it generates crash dumps and kills the process.
    @CalledByNative
    private void listenForUncaughtExceptionsForTesting() {
        mThread.setUncaughtExceptionHandler(new UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                mUnhandledException = e;
            }
        });
    }

    @CalledByNative
    private Throwable getUncaughtExceptionIfAny() {
        return mUnhandledException;
    }

    private native void nativeInitializeThread(long nativeJavaHandlerThread, long nativeEvent);
    private native void nativeStopThread(long nativeJavaHandlerThread);
    private native void nativeOnLooperStopped(long nativeJavaHandlerThread);
}
