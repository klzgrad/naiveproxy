// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.MessageQueue.IdleHandler;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

@MainDex
@JNINamespace("base")
class SystemMessageHandler extends Handler {
    private static final String TAG = "cr.SysMessageHandler";

    private static final int SCHEDULED_WORK = 1;
    private static final int DELAYED_SCHEDULED_WORK = 2;

    private long mNativeMessagePumpForUI;
    private boolean mScheduledDelayedWork;

    private final IdleHandler mIdleHandler = new IdleHandler() {
        @Override
        public boolean queueIdle() {
            if (mNativeMessagePumpForUI == 0) return false;
            nativeDoIdleWork(mNativeMessagePumpForUI);
            return true;
        }
    };

    protected SystemMessageHandler(long nativeMessagePumpForUI) {
        mNativeMessagePumpForUI = nativeMessagePumpForUI;
        Looper.myQueue().addIdleHandler(mIdleHandler);
    }

    @Override
    public void handleMessage(Message msg) {
        if (mNativeMessagePumpForUI == 0) return;
        boolean delayed = msg.what == DELAYED_SCHEDULED_WORK;
        if (delayed) mScheduledDelayedWork = false;
        nativeDoRunLoopOnce(mNativeMessagePumpForUI, delayed);
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void scheduleWork() {
        sendMessage(obtainAsyncMessage(SCHEDULED_WORK));
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void scheduleDelayedWork(long millis) {
        if (mScheduledDelayedWork) removeMessages(DELAYED_SCHEDULED_WORK);
        mScheduledDelayedWork = true;
        sendMessageDelayed(obtainAsyncMessage(DELAYED_SCHEDULED_WORK), millis);
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void shutdown() {
        // No need to perform a slow removeMessages call, we should have executed all of the
        // outstanding tasks already, and if there happen to be any left, we'll ignore them.
        // The idleHandler will also remove itself next time the queue goes idle, so no need to
        // remove it here.
        mNativeMessagePumpForUI = 0;
    }

    private Message obtainAsyncMessage(int what) {
        // Marking the message async provides fair Chromium task dispatch when
        // served by the Android UI thread's Looper, avoiding stalls when the
        // Looper has a sync barrier.
        Message msg = Message.obtain();
        msg.what = what;
        MessageCompat.setAsynchronous(msg, true);
        return msg;
    }

    /**
     * Abstraction utility class for marking a Message as asynchronous. Prior
     * to L MR1 the async Message API was hidden, and for such cases we fall
     * back to using reflection to obtain the necessary method.
     */
    private static class MessageCompat {
        /**
         * @See android.os.Message#setAsynchronous(boolean)
         */
        public static void setAsynchronous(Message message, boolean async) {
            IMPL.setAsynchronous(message, async);
        }

        interface MessageWrapperImpl {
            /**
             * @See android.os.Message#setAsynchronous(boolean)
             */
            public void setAsynchronous(Message message, boolean async);
        }

        static final MessageWrapperImpl IMPL;
        static {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP_MR1) {
                IMPL = new LollipopMr1MessageWrapperImpl();
            } else {
                IMPL = new LegacyMessageWrapperImpl();
            }
        }

        static class LollipopMr1MessageWrapperImpl implements MessageWrapperImpl {
            @SuppressLint("NewApi")
            @Override
            public void setAsynchronous(Message msg, boolean async) {
                msg.setAsynchronous(async);
            }
        }

        static class LegacyMessageWrapperImpl implements MessageWrapperImpl {
            // Reflected API for marking a message as asynchronous.
            // Note: Use of this API is experimental and likely to evolve in the future.
            private Method mMessageMethodSetAsynchronous;

            LegacyMessageWrapperImpl() {
                try {
                    mMessageMethodSetAsynchronous =
                            Message.class.getMethod("setAsynchronous", new Class[] {boolean.class});
                } catch (NoSuchMethodException e) {
                    Log.e(TAG, "Failed to load Message.setAsynchronous method", e);
                } catch (RuntimeException e) {
                    Log.e(TAG, "Exception while loading Message.setAsynchronous method", e);
                }
            }

            @Override
            public void setAsynchronous(Message msg, boolean async) {
                if (mMessageMethodSetAsynchronous == null) return;
                // If invocation fails, assume this is indicative of future
                // failures, and avoid log spam by nulling the reflected method.
                try {
                    mMessageMethodSetAsynchronous.invoke(msg, async);
                } catch (IllegalAccessException e) {
                    Log.e(TAG, "Illegal access to async message creation, disabling.");
                    mMessageMethodSetAsynchronous = null;
                } catch (IllegalArgumentException e) {
                    Log.e(TAG, "Illegal argument for async message creation, disabling.");
                    mMessageMethodSetAsynchronous = null;
                } catch (InvocationTargetException e) {
                    Log.e(TAG, "Invocation exception during async message creation, disabling.");
                    mMessageMethodSetAsynchronous = null;
                } catch (RuntimeException e) {
                    Log.e(TAG, "Runtime exception during async message creation, disabling.");
                    mMessageMethodSetAsynchronous = null;
                }
            }
        }
    }

    @CalledByNative
    private static SystemMessageHandler create(long nativeMessagePumpForUI) {
        return new SystemMessageHandler(nativeMessagePumpForUI);
    }

    private native void nativeDoRunLoopOnce(long nativeMessagePumpForUI, boolean delayed);
    private native void nativeDoIdleWork(long nativeMessagePumpForUI);
}
