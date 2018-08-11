// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.res.AssetManager;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Build.VERSION_CODES;
import android.os.Handler;
import android.os.Looper;
import android.support.v4.content.ContextCompat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipFile;

/**
 * Handles extracting the necessary resources bundled in an APK and moving them to a location on
 * the file system accessible from the native code.
 */
public class ResourceExtractor {
    // Experience shows that on some devices, the PackageManager fails to properly extract
    // native shared libraries to the /data partition at installation or upgrade time,
    // which creates all kind of chaos (https://crbug.com/806998).
    //
    // We implement a fallback when we detect the issue by manually extracting the library
    // into Chromium's own data directory, then retrying to load the new library from here.
    //
    // This will work for any device running K-. Starting with Android L, render processes
    // cannot access the file system anymore, and extraction will always fail for them.
    // However, the issue doesn't seem to appear in the field for Android L.
    //
    // Also, starting with M, the issue doesn't exist if shared libraries are stored
    // uncompressed in the APK (as Chromium does), because the system linker can access them
    // directly, and the PackageManager will thus never extract them in the first place.
    static public final boolean PLATFORM_REQUIRES_NATIVE_FALLBACK_EXTRACTION =
            Build.VERSION.SDK_INT <= VERSION_CODES.KITKAT;

    private static final String TAG = "base";
    private static final String ICU_DATA_FILENAME = "icudtl.dat";
    private static final String V8_NATIVES_DATA_FILENAME = "natives_blob.bin";
    private static final String V8_SNAPSHOT_DATA_FILENAME = "snapshot_blob.bin";
    private static final String FALLBACK_LOCALE = "en-US";
    private static final String LIBRARY_DIR = "native_libraries";
    private static final String COMPRESSED_LOCALES_DIR = "locales";
    private static final int BUFFER_SIZE = 16 * 1024;

    private class ExtractTask extends AsyncTask<Void, Void, Void> {

        private final List<Runnable> mCompletionCallbacks = new ArrayList<Runnable>();

        private void doInBackgroundImpl() {
            final File outputDir = getOutputDir();
            if (!outputDir.exists() && !outputDir.mkdirs()) {
                throw new RuntimeException();
            }

            // Use a suffix for extracted files in order to guarantee that the version of the file
            // on disk matches up with the version of the APK.
            String extractSuffix = BuildInfo.getInstance().extractedFileSuffix;
            String[] existingFileNames = outputDir.list();
            boolean allFilesExist = existingFileNames != null;
            if (allFilesExist) {
                List<String> existingFiles = Arrays.asList(existingFileNames);
                for (String assetName : mAssetsToExtract) {
                    allFilesExist &= existingFiles.contains(assetName + extractSuffix);
                }
            }
            // This is the normal case.
            if (allFilesExist) {
                return;
            }
            // A missing file means Chrome has updated. Delete stale files first.
            deleteFiles(existingFileNames);

            AssetManager assetManager = ContextUtils.getApplicationAssets();
            byte[] buffer = new byte[BUFFER_SIZE];
            for (String assetPath : mAssetsToExtract) {
                String assetName = assetPath.substring(assetPath.lastIndexOf('/') + 1);
                File output = new File(outputDir, assetName + extractSuffix);
                TraceEvent.begin("ExtractResource");
                try (InputStream inputStream = assetManager.open(assetPath)) {
                    extractResourceHelper(inputStream, output, buffer);
                } catch (IOException e) {
                    // The app would just crash later if files are missing.
                    throw new RuntimeException(e);
                } finally {
                    TraceEvent.end("ExtractResource");
                }
            }
        }

        @Override
        protected Void doInBackground(Void... unused) {
            TraceEvent.begin("ResourceExtractor.ExtractTask.doInBackground");
            try {
                doInBackgroundImpl();
            } finally {
                TraceEvent.end("ResourceExtractor.ExtractTask.doInBackground");
            }
            return null;
        }

        private void onPostExecuteImpl() {
            for (int i = 0; i < mCompletionCallbacks.size(); i++) {
                mCompletionCallbacks.get(i).run();
            }
            mCompletionCallbacks.clear();
        }

        @Override
        protected void onPostExecute(Void result) {
            TraceEvent.begin("ResourceExtractor.ExtractTask.onPostExecute");
            try {
                onPostExecuteImpl();
            } finally {
                TraceEvent.end("ResourceExtractor.ExtractTask.onPostExecute");
            }
        }
    }

    private ExtractTask mExtractTask;
    private final String[] mAssetsToExtract = detectFilesToExtract();

    private static ResourceExtractor sInstance;

    public static ResourceExtractor get() {
        if (sInstance == null) {
            sInstance = new ResourceExtractor();
        }
        return sInstance;
    }

    // Android system sometimes fails to extract libraries from APK (https://crbug.com/806998).
    // This function manually extract libraries as a fallback.
    @SuppressLint({"SetWorldReadable"})
    public static String extractFileIfStale(
            Context appContext, String pathWithinApk, File destDir) {
        assert PLATFORM_REQUIRES_NATIVE_FALLBACK_EXTRACTION;

        String apkPath = appContext.getApplicationInfo().sourceDir;
        String fileName =
                (new File(pathWithinApk)).getName() + BuildInfo.getInstance().extractedFileSuffix;
        File libraryFile = new File(destDir, fileName);

        if (!libraryFile.exists()) {
            try (ZipFile zipFile = new ZipFile(apkPath);
                    InputStream inputStream =
                            zipFile.getInputStream(zipFile.getEntry(pathWithinApk))) {
                if (zipFile.getEntry(pathWithinApk) == null)
                    throw new RuntimeException("Cannot find ZipEntry" + pathWithinApk);

                extractResourceHelper(inputStream, libraryFile, new byte[BUFFER_SIZE]);
                libraryFile.setReadable(true, false);
                libraryFile.setExecutable(true, false);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        }
        return libraryFile.getAbsolutePath();
    }

    public static File makeLibraryDirAndSetPermission() {
        if (!ContextUtils.isIsolatedProcess()) {
            File cacheDir = ContextCompat.getCodeCacheDir(ContextUtils.getApplicationContext());
            File libDir = new File(cacheDir, LIBRARY_DIR);
            cacheDir.mkdir();
            cacheDir.setExecutable(true, false);
            libDir.mkdir();
            libDir.setExecutable(true, false);
        }
        return getLibraryDir();
    }

    private static File getLibraryDir() {
        return new File(
                ContextCompat.getCodeCacheDir(ContextUtils.getApplicationContext()), LIBRARY_DIR);
    }

    private static void extractResourceHelper(InputStream is, File outFile, byte[] buffer)
            throws IOException {
        File tmpOutputFile = new File(outFile.getPath() + ".tmp");
        try (OutputStream os = new FileOutputStream(tmpOutputFile)) {
            Log.i(TAG, "Extracting resource %s", outFile);

            int count = 0;
            while ((count = is.read(buffer, 0, BUFFER_SIZE)) != -1) {
                os.write(buffer, 0, count);
            }
        }
        if (!tmpOutputFile.renameTo(outFile)) {
            throw new IOException();
        }
    }

    private static String[] detectFilesToExtract() {
        Locale defaultLocale = Locale.getDefault();
        String language = LocaleUtils.getUpdatedLanguageForChromium(defaultLocale.getLanguage());
        // Currenty (Apr 2018), this array can be as big as 6 entries, so using a capacity
        // that allows a bit of growth, but is still in the right ballpark..
        ArrayList<String> activeLocales = new ArrayList<String>(6);
        for (String locale : BuildConfig.COMPRESSED_LOCALES) {
            if (locale.startsWith(language)) {
                activeLocales.add(locale);
            }
        }
        if (activeLocales.isEmpty() && BuildConfig.COMPRESSED_LOCALES.length > 0) {
            assert Arrays.asList(BuildConfig.COMPRESSED_LOCALES).contains(FALLBACK_LOCALE);
            activeLocales.add(FALLBACK_LOCALE);
        }
        String[] localePakFiles = new String[activeLocales.size()];
        for (int n = 0; n < activeLocales.size(); ++n) {
            localePakFiles[n] = COMPRESSED_LOCALES_DIR + '/' + activeLocales.get(n) + ".pak";
        }
        Log.i(TAG, "Android Locale: %s requires .pak files: %s", defaultLocale,
                Arrays.toString(activeLocales.toArray()));

        return localePakFiles;
    }

    /**
     * Synchronously wait for the resource extraction to be completed.
     * <p>
     * This method is bad and you should feel bad for using it.
     *
     * @see #addCompletionCallback(Runnable)
     */
    public void waitForCompletion() {
        if (mExtractTask == null || shouldSkipPakExtraction()) {
            return;
        }

        try {
            mExtractTask.get();
        } catch (Exception e) {
            assert false;
        }
    }

    /**
     * Adds a callback to be notified upon the completion of resource extraction.
     * <p>
     * If the resource task has already completed, the callback will be posted to the UI message
     * queue.  Otherwise, it will be executed after all the resources have been extracted.
     * <p>
     * This must be called on the UI thread.  The callback will also always be executed on
     * the UI thread.
     *
     * @param callback The callback to be enqueued.
     */
    public void addCompletionCallback(Runnable callback) {
        ThreadUtils.assertOnUiThread();

        Handler handler = new Handler(Looper.getMainLooper());
        if (shouldSkipPakExtraction()) {
            handler.post(callback);
            return;
        }

        assert mExtractTask != null;
        assert !mExtractTask.isCancelled();
        if (mExtractTask.getStatus() == AsyncTask.Status.FINISHED) {
            handler.post(callback);
        } else {
            mExtractTask.mCompletionCallbacks.add(callback);
        }
    }

    /**
     * This will extract the application pak resources in an
     * AsyncTask. Call waitForCompletion() at the point resources
     * are needed to block until the task completes.
     */
    public void startExtractingResources() {
        if (mExtractTask != null) {
            return;
        }

        // If a previous release extracted resources, and the current release does not,
        // deleteFiles() will not run and some files will be left. This currently
        // can happen for ContentShell, but not for Chrome proper, since we always extract
        // locale pak files.
        if (shouldSkipPakExtraction()) {
            return;
        }

        mExtractTask = new ExtractTask();
        mExtractTask.executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);
    }

    private File getAppDataDir() {
        return new File(PathUtils.getDataDirectory());
    }

    private File getOutputDir() {
        return new File(getAppDataDir(), "paks");
    }

    private static void deleteFile(File file) {
        if (file.exists() && !file.delete()) {
            Log.w(TAG, "Unable to remove %s", file.getName());
        }
    }

    private void deleteFiles(String[] existingFileNames) {
        // These used to be extracted, but no longer are, so just clean them up.
        deleteFile(new File(getAppDataDir(), ICU_DATA_FILENAME));
        deleteFile(new File(getAppDataDir(), V8_NATIVES_DATA_FILENAME));
        deleteFile(new File(getAppDataDir(), V8_SNAPSHOT_DATA_FILENAME));

        if (PLATFORM_REQUIRES_NATIVE_FALLBACK_EXTRACTION) {
            String suffix = BuildInfo.getInstance().extractedFileSuffix;
            File[] files = getLibraryDir().listFiles();
            if (files != null) {
                for (File file : files) {
                    // The delete can happen on the same time as writing file from InputStream, use
                    // contains() to avoid deleting the temp file.
                    if (!file.getName().contains(suffix)) {
                        deleteFile(file);
                    }
                }
            }
        }

        if (existingFileNames != null) {
            for (String fileName : existingFileNames) {
                deleteFile(new File(getOutputDir(), fileName));
            }
        }
    }

    /**
     * Pak extraction not necessarily required by the embedder.
     */
    private static boolean shouldSkipPakExtraction() {
        return get().mAssetsToExtract.length == 0;
    }
}
