// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.compat;

import android.annotation.TargetApi;
import android.app.Activity;
import android.app.job.JobInfo;
import android.app.job.JobScheduler;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.media.MediaCodec.CryptoInfo;
import android.os.Build;
import android.view.PointerIcon;
import android.view.View;
import android.view.View.DragShadowBuilder;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.chromium.base.annotations.DoNotInline;

/**
 * Utility class to use new APIs that were added in N (API level 24). These need to exist in a
 * separate class so that Android framework can successfully verify classes without
 * encountering the new APIs.
 */
@DoNotInline
@TargetApi(Build.VERSION_CODES.N)
public final class ApiHelperForN {
    private ApiHelperForN() {}

    /**
     * See {@link WebViewClient#shouldOverrideUrlLoading(WebView, WebResourceRequest)}, which was
     * added in N.
     */
    public static boolean shouldOverrideUrlLoading(
            WebViewClient webViewClient, WebView webView, WebResourceRequest request) {
        return webViewClient.shouldOverrideUrlLoading(webView, request);
    }

    /** See {@link JobScheduler#getPendingJob(int)}. */
    public static JobInfo getPendingJob(JobScheduler scheduler, int jobId) {
        return scheduler.getPendingJob(jobId);
    }

    /** See {@link View#startDragAndDrop(ClipData, DragShadowBuilder, Object, int)}. */
    public static boolean startDragAndDrop(View view, ClipData data,
            DragShadowBuilder shadowBuilder, Object myLocalState, int flags) {
        return view.startDragAndDrop(data, shadowBuilder, myLocalState, flags);
    }

    /** See {@link View#setPointerIcon(PointerIcon)}. */
    public static void setPointerIcon(View view, PointerIcon icon) {
        view.setPointerIcon(icon);
    }

    /** See {@link PointerIcon#create(Bitmap, float, float)}. */
    public static PointerIcon createPointerIcon(Bitmap bitmap, float width, float height) {
        return PointerIcon.create(bitmap, width, height);
    }

    /** See {@link CryptoInfo#setPattern(Pattern)}. */
    public static void setCryptoInfoPattern(CryptoInfo cryptoInfo, int encrypt, int skip) {
        cryptoInfo.setPattern(new CryptoInfo.Pattern(encrypt, skip));
    }

    /** See {@link Activity#setVrModeEnabled(boolean, ComponentName)}. */
    public static void setVrModeEnabled(Activity activity, boolean enabled,
            ComponentName requestedComponent) throws PackageManager.NameNotFoundException {
        activity.setVrModeEnabled(enabled, requestedComponent);
    }
}
