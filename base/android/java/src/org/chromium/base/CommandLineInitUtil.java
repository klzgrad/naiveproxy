// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.annotation.SuppressLint;
import android.content.Context;
import android.os.Build;
import android.provider.Settings;

import org.chromium.base.annotations.SuppressFBWarnings;

import java.io.File;

/**
 * Provides implementation of command line initialization for Android.
 */
public final class CommandLineInitUtil {
    private static final String TAG = "CommandLineInitUtil";

    /**
     * The location of the command line file needs to be in a protected
     * directory so requires root access to be tweaked, i.e., no other app in a
     * regular (non-rooted) device can change this file's contents.
     * See below for debugging on a regular (non-rooted) device.
     */
    private static final String COMMAND_LINE_FILE_PATH = "/data/local";

    /**
     * This path (writable by the shell in regular non-rooted "user" builds) is used when:
     * 1) The "debug app" is set to the application calling this.
     * and
     * 2) ADB is enabled.
     *
     */
    private static final String COMMAND_LINE_FILE_PATH_DEBUG_APP = "/data/local/tmp";

    private CommandLineInitUtil() {
    }

    /**
     * Initializes the CommandLine class, pulling command line arguments from {@code fileName}.
     * @param context  The {@link Context} to use to query whether or not this application is being
     *                 debugged, and whether or not the publicly writable command line file should
     *                 be used.
     * @param fileName The name of the command line file to pull arguments from.
     */
    @SuppressFBWarnings("DMI_HARDCODED_ABSOLUTE_FILENAME")
    public static void initCommandLine(Context context, String fileName) {
        if (!CommandLine.isInitialized()) {
            File commandLineFile = getAlternativeCommandLinePath(context, fileName);
            if (commandLineFile != null) {
                Log.i(TAG,
                        "Initializing command line from alternative file "
                                + commandLineFile.getPath());
            } else {
                commandLineFile = new File(COMMAND_LINE_FILE_PATH, fileName);
                Log.d(TAG, "Initializing command line from " + commandLineFile.getPath());
            }
            CommandLine.initFromFile(commandLineFile.getPath());
        }
    }

    /**
     * Use an alternative path if:
     * - The current build is "eng" or "userdebug", OR
     * - adb is enabled and this is the debug app.
     */
    @SuppressFBWarnings("DMI_HARDCODED_ABSOLUTE_FILENAME")
    private static File getAlternativeCommandLinePath(Context context, String fileName) {
        File alternativeCommandLineFile =
                new File(COMMAND_LINE_FILE_PATH_DEBUG_APP, fileName);
        if (!alternativeCommandLineFile.exists()) return null;
        try {
            if (BuildInfo.isDebugAndroid()) {
                return alternativeCommandLineFile;
            }

            String debugApp = Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN_MR1
                    ? getDebugAppPreJBMR1(context)
                    : getDebugAppJBMR1(context);

            if (debugApp != null
                    && debugApp.equals(context.getApplicationContext().getPackageName())) {
                return alternativeCommandLineFile;
            }
        } catch (RuntimeException e) {
            Log.e(TAG, "Unable to detect alternative command line file");
        }

        return null;
    }

    @SuppressLint("NewApi")
    private static String getDebugAppJBMR1(Context context) {
        boolean adbEnabled = Settings.Global.getInt(context.getContentResolver(),
                Settings.Global.ADB_ENABLED, 0) == 1;
        if (adbEnabled) {
            return Settings.Global.getString(context.getContentResolver(),
                    Settings.Global.DEBUG_APP);
        }
        return null;
    }

    @SuppressWarnings("deprecation")
    private static String getDebugAppPreJBMR1(Context context) {
        boolean adbEnabled = Settings.System.getInt(context.getContentResolver(),
                Settings.System.ADB_ENABLED, 0) == 1;
        if (adbEnabled) {
            return Settings.System.getString(context.getContentResolver(),
                    Settings.System.DEBUG_APP);
        }
        return null;
    }
}
