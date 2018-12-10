// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.compat;

import android.annotation.TargetApi;
import android.os.Build;
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
}
