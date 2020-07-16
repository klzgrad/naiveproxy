// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.annotation.TargetApi;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.telephony.PhoneStateListener;
import android.telephony.SignalStrength;
import android.telephony.TelephonyManager;

import org.chromium.base.ApplicationState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.ContextUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

/**
 * This class provides the cellular signal strength using the APIs provided by Android. This class
 * is thread safe.
 */
@JNINamespace("net::android")
public class AndroidCellularSignalStrength {
    // {@link mSignalLevel} is set to volatile since may be accessed across threads.
    private volatile int mSignalLevel = CellularSignalStrengthError.ERROR_NOT_SUPPORTED;

    private static final AndroidCellularSignalStrength sInstance =
            new AndroidCellularSignalStrength();

    /**
     * This class listens to the changes in the cellular signal strength level and updates {@link
     * mSignalLevel}. {@link CellStateListener} registers as a signal strength observer only if the
     * application has running activities.
     */
    private class CellStateListener
            extends PhoneStateListener implements ApplicationStatus.ApplicationStateListener {
        private final TelephonyManager mTelephonyManager;

        CellStateListener() {
            ThreadUtils.assertOnBackgroundThread();

            mTelephonyManager =
                    (TelephonyManager) ContextUtils.getApplicationContext().getSystemService(
                            Context.TELEPHONY_SERVICE);

            if (mTelephonyManager.getSimState() != TelephonyManager.SIM_STATE_READY) return;

            ApplicationStatus.registerApplicationStateListener(this);
            onApplicationStateChange(ApplicationStatus.getStateForApplication());
        }

        private void register() {
            mTelephonyManager.listen(this, PhoneStateListener.LISTEN_SIGNAL_STRENGTHS);
        }

        private void unregister() {
            mSignalLevel = CellularSignalStrengthError.ERROR_NOT_SUPPORTED;
            mTelephonyManager.listen(this, PhoneStateListener.LISTEN_NONE);
        }

        @Override
        @TargetApi(Build.VERSION_CODES.M)
        public void onSignalStrengthsChanged(SignalStrength signalStrength) {
            if (ApplicationStatus.getStateForApplication()
                    != ApplicationState.HAS_RUNNING_ACTIVITIES) {
                return;
            }
            try {
                mSignalLevel = signalStrength.getLevel();
            } catch (SecurityException e) {
                // Catch any exceptions thrown due to unavailability of permissions on certain
                // Android devices. See  https://crbug.com/820564 for details.
                mSignalLevel = CellularSignalStrengthError.ERROR_NOT_SUPPORTED;
                assert false;
            }
        }

        // ApplicationStatus.ApplicationStateListener
        @Override
        public void onApplicationStateChange(int newState) {
            if (newState == ApplicationState.HAS_RUNNING_ACTIVITIES) {
                register();
            } else if (newState == ApplicationState.HAS_PAUSED_ACTIVITIES) {
                unregister();
            }
        }
    }

    private AndroidCellularSignalStrength() {
        // {@link android.telephony.SignalStrength#getLevel} is only available on API Level
        // {@link Build.VERSION_CODES#M} and higher.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return;

        HandlerThread handlerThread = new HandlerThread("AndroidCellularSignalStrength");
        handlerThread.start();

        new Handler(handlerThread.getLooper()).post(new Runnable() {
            @Override
            public void run() {
                new CellStateListener();
            }
        });
    }

    /**
     * @return the signal strength level (between 0 and 4, both inclusive) for the currently
     * registered cellular network with lower value indicating lower signal strength. Returns
     * {@link CellularSignalStrengthError#ERROR_NOT_SUPPORTED} if the signal strength level is
     * unavailable.
     */
    @TargetApi(Build.VERSION_CODES.M)
    @CalledByNative
    private static int getSignalStrengthLevel() {
        return sInstance.mSignalLevel;
    }
}
