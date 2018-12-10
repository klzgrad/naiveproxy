// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

/**
 * This class provides package checking related methods.
 */
public class PackageUtils {
    /**
     * Retrieves the version of the given package installed on the device.
     *
     * @param context Any context.
     * @param packageName Name of the package to find.
     * @return The package's version code if found, -1 otherwise.
     */
    public static int getPackageVersion(Context context, String packageName) {
        int versionCode = -1;
        PackageManager pm = context.getPackageManager();
        try {
            PackageInfo packageInfo = pm.getPackageInfo(packageName, 0);
            if (packageInfo != null) versionCode = packageInfo.versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            // Do nothing, versionCode stays -1
        }
        return versionCode;
    }

    /**
     * Decodes into a Bitmap an Image resource stored in another package.
     * @param otherPackage The package containing the resource.
     * @param resourceId The id of the resource.
     * @return A Bitmap containing the resource or null if the package could not be found.
     */
    public static Bitmap decodeImageResource(String otherPackage, int resourceId) {
        PackageManager packageManager = ContextUtils.getApplicationContext().getPackageManager();
        try {
            Resources resources = packageManager.getResourcesForApplication(otherPackage);
            return BitmapFactory.decodeResource(resources, resourceId);
        } catch (PackageManager.NameNotFoundException e) {
            return null;
        }
    }

    private PackageUtils() {
        // Hide constructor
    }
}
