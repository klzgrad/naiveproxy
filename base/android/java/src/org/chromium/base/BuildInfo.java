// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.os.Build;
import android.os.Build.VERSION;
import android.text.TextUtils;

import org.chromium.base.annotations.CalledByNative;

/**
 * BuildInfo is a utility class providing easy access to {@link PackageInfo} information. This is
 * primarily of use for accessing package information from native code.
 */
public class BuildInfo {
    /**
     * Array index to access field in {@link BuildInfo#getAll()}.
     */
    public static final int BRAND_INDEX = 0;
    public static final int DEVICE_INDEX = 1;
    public static final int ANDROID_BUILD_ID_INDEX = 2;
    public static final int MODEL_INDEX = 4;
    public static final int ANDROID_BUILD_FP_INDEX = 11;
    public static final int GMS_CORE_VERSION_INDEX = 12;
    public static final int INSTALLER_PACKAGE_NAME_INDEX = 13;
    public static final int ABI_NAME_INDEX = 14;

    private static final String TAG = "BuildInfo";
    private static final int MAX_FINGERPRINT_LENGTH = 128;

    /**
     * BuildInfo is a static utility class and therefore shouldn't be instantiated.
     */
    private BuildInfo() {}

    @SuppressWarnings("deprecation")
    @CalledByNative
    public static String[] getAll() {
        try {
            String packageName = ContextUtils.getApplicationContext().getPackageName();
            PackageManager pm = ContextUtils.getApplicationContext().getPackageManager();
            PackageInfo pi = pm.getPackageInfo(packageName, 0);
            String versionCode = pi.versionCode <= 0 ? "" : Integer.toString(pi.versionCode);
            String versionName = pi.versionName == null ? "" : pi.versionName;

            CharSequence label = pm.getApplicationLabel(pi.applicationInfo);
            String packageLabel = label == null ? "" : label.toString();

            String installerPackageName = pm.getInstallerPackageName(packageName);
            if (installerPackageName == null) {
                installerPackageName = "";
            }

            String abiString = null;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                abiString = TextUtils.join(", ", Build.SUPPORTED_ABIS);
            } else {
                abiString = "ABI1: " + Build.CPU_ABI + ", ABI2: " + Build.CPU_ABI2;
            }

            // Use lastUpdateTime when developing locally, since versionCode does not normally
            // change in this case.
            long version = pi.versionCode > 10 ? pi.versionCode : pi.lastUpdateTime;
            String extractedFileSuffix = String.format("@%s", Long.toHexString(version));

            // Do not alter this list without updating callers of it.
            return new String[] {
                    Build.BRAND, Build.DEVICE, Build.ID, Build.MANUFACTURER, Build.MODEL,
                    String.valueOf(Build.VERSION.SDK_INT), Build.TYPE, packageLabel, packageName,
                    versionCode, versionName, getAndroidBuildFingerprint(), getGMSVersionCode(pm),
                    installerPackageName, abiString, extractedFileSuffix,
            };
        } catch (NameNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    /**
     * @return The build fingerprint for the current Android install.  The value is truncated to a
     * 128 characters as this is used for crash and UMA reporting, which should avoid huge
     * strings.
     */
    private static String getAndroidBuildFingerprint() {
        return Build.FINGERPRINT.substring(
                0, Math.min(Build.FINGERPRINT.length(), MAX_FINGERPRINT_LENGTH));
    }

    private static String getGMSVersionCode(PackageManager packageManager) {
        String msg = "gms versionCode not available.";
        try {
            PackageInfo packageInfo = packageManager.getPackageInfo("com.google.android.gms", 0);
            msg = Integer.toString(packageInfo.versionCode);
        } catch (NameNotFoundException e) {
            Log.d(TAG, "GMS package is not found.", e);
        }
        return msg;
    }

    public static String getPackageVersionName() {
        return getAll()[10];
    }

    /** Returns a string that is different each time the apk changes. */
    public static String getExtractedFileSuffix() {
        return getAll()[15];
    }

    public static String getPackageLabel() {
        return getAll()[7];
    }

    public static String getPackageName() {
        return ContextUtils.getApplicationContext().getPackageName();
    }

    /**
     * Check if this is a debuggable build of Android. Use this to enable developer-only features.
     */
    public static boolean isDebugAndroid() {
        return "eng".equals(Build.TYPE) || "userdebug".equals(Build.TYPE);
    }

    // The markers Begin:BuildCompat and End:BuildCompat delimit code
    // that is autogenerated from Android sources.
    // Begin:BuildCompat O,OMR1,P

    /**
     * Checks if the device is running on a pre-release version of Android O or newer.
     * <p>
     * @return {@code true} if O APIs are available for use, {@code false} otherwise
     * @deprecated Android O is a finalized release and this method is no longer necessary. It will
     *             be removed in a future release of the Support Library. Instead use
     *             {@code Build.SDK_INT >= Build.VERSION_CODES#O}.
     */
    @Deprecated
    public static boolean isAtLeastO() {
        return VERSION.SDK_INT >= 26;
    }

    /**
     * Checks if the device is running on a pre-release version of Android O MR1 or newer.
     * <p>
     * @return {@code true} if O MR1 APIs are available for use, {@code false} otherwise
     * @deprecated Android O MR1 is a finalized release and this method is no longer necessary. It
     *             will be removed in a future release of the Support Library. Instead, use
     *             {@code Build.SDK_INT >= Build.VERSION_CODES#O_MR1}.
     */
    @Deprecated
    public static boolean isAtLeastOMR1() {
        return VERSION.SDK_INT >= 27;
    }

    /**
     * Checks if the device is running on a pre-release version of Android P or newer.
     * <p>
     * <strong>Note:</strong> This method will return {@code false} on devices running release
     * versions of Android. When Android P is finalized for release, this method will be deprecated
     * and all calls should be replaced with {@code Build.SDK_INT >= Build.VERSION_CODES#P}.
     *
     * @return {@code true} if P APIs are available for use, {@code false} otherwise
     */
    public static boolean isAtLeastP() {
        return VERSION.CODENAME.equals("P");
    }

    /**
     * Checks if the application targets at least released SDK O
     */
    @Deprecated
    public static boolean targetsAtLeastO() {
        return ContextUtils.getApplicationContext().getApplicationInfo().targetSdkVersion >= 26;
    }

    /**
     * Checks if the application targets at least released SDK OMR1
     */
    @Deprecated
    public static boolean targetsAtLeastOMR1() {
        return ContextUtils.getApplicationContext().getApplicationInfo().targetSdkVersion >= 27;
    }

    /**
     * Checks if the application targets pre-release SDK P
     */
    public static boolean targetsAtLeastP() {
        return isAtLeastP()
                && ContextUtils.getApplicationContext().getApplicationInfo().targetSdkVersion
                == Build.VERSION_CODES.CUR_DEVELOPMENT;
    }

    // End:BuildCompat
}
