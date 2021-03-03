// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.compat;

import android.annotation.TargetApi;
import android.content.Context;
import android.os.Build;
import android.view.Display;

import org.chromium.base.annotations.VerifiesOnR;

/**
 * Utility class to use new APIs that were added in Q (API level 29). These need to exist in a
 * separate class so that Android framework can successfully verify classes without
 * encountering the new APIs.
 */
@VerifiesOnR
@TargetApi(Build.VERSION_CODES.R)
public final class ApiHelperForR {
    private ApiHelperForR() {}

    public static Display getDisplay(Context context) throws UnsupportedOperationException {
        return context.getDisplay();
    }
}
