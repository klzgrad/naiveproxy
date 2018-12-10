// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.compat;

import android.annotation.TargetApi;
import android.os.Build;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.chromium.base.annotations.DoNotInline;

/**
 * Utility class to use new APIs that were added in M (API level 23). These need to exist in a
 * separate class so that Android framework can successfully verify classes without
 * encountering the new APIs.
 */
@DoNotInline
@TargetApi(Build.VERSION_CODES.M)
public final class ApiHelperForM {
    private ApiHelperForM() {}

    /**
     * See {@link WebViewClient#onPageCommitVisible(WebView, String)}, which was added in M.
     */
    public static void onPageCommitVisible(
            WebViewClient webViewClient, WebView webView, String url) {
        webViewClient.onPageCommitVisible(webView, url);
    }
}
