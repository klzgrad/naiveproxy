// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.content.Context;
import android.net.Uri;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

/**
 * Helper methods for dealing with Files.
 */
public class FileUtils {
    private static final String TAG = "FileUtils";

    /**
     * Delete the given File and (if it's a directory) everything within it.
     */
    public static void recursivelyDeleteFile(File currentFile) {
        ThreadUtils.assertOnBackgroundThread();
        if (currentFile.isDirectory()) {
            File[] files = currentFile.listFiles();
            if (files != null) {
                for (File file : files) {
                    recursivelyDeleteFile(file);
                }
            }
        }

        if (!currentFile.delete()) Log.e(TAG, "Failed to delete: " + currentFile);
    }

    /**
     * Delete the given files or directories by calling {@link #recursivelyDeleteFile(File)}.
     * @param files The files to delete.
     */
    public static void batchDeleteFiles(List<File> files) {
        ThreadUtils.assertOnBackgroundThread();

        for (File file : files) {
            if (file.exists()) recursivelyDeleteFile(file);
        }
    }

    /**
     * Extracts an asset from the app's APK to a file.
     * @param context
     * @param assetName Name of the asset to extract.
     * @param dest File to extract the asset to.
     * @return true on success.
     */
    public static boolean extractAsset(Context context, String assetName, File dest) {
        InputStream inputStream = null;
        OutputStream outputStream = null;
        try {
            inputStream = context.getAssets().open(assetName);
            outputStream = new BufferedOutputStream(new FileOutputStream(dest));
            byte[] buffer = new byte[8192];
            int c;
            while ((c = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, c);
            }
            inputStream.close();
            outputStream.close();
            return true;
        } catch (IOException e) {
            if (inputStream != null) {
                try {
                    inputStream.close();
                } catch (IOException ex) {
                }
            }
            if (outputStream != null) {
                try {
                    outputStream.close();
                } catch (IOException ex) {
                }
            }
        }
        return false;
    }

    /**
     * Returns a URI that points at the file.
     * @param file File to get a URI for.
     * @return URI that points at that file, either as a content:// URI or a file:// URI.
     */
    public static Uri getUriForFile(File file) {
        // TODO(crbug/709584): Uncomment this when http://crbug.com/709584 has been fixed.
        // assert !ThreadUtils.runningOnUiThread();
        Uri uri = null;

        try {
            // Try to obtain a content:// URI, which is preferred to a file:// URI so that
            // receiving apps don't attempt to determine the file's mime type (which often fails).
            uri = ContentUriUtils.getContentUriFromFile(file);
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "Could not create content uri: " + e);
        }

        if (uri == null) uri = Uri.fromFile(file);

        return uri;
    }
}
