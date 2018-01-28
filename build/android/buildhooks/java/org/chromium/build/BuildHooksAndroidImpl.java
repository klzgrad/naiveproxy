// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.build;

import android.content.Context;
import android.content.res.AssetManager;
import android.content.res.Resources;

/**
 * Instantiatable version of {@link BuildHooksAndroid} with dummy implementations.
 */
public class BuildHooksAndroidImpl extends BuildHooksAndroid {
    protected final Resources getResourcesImpl(Context context) {
        return null;
    }

    protected AssetManager getAssetsImpl(Context context) {
        return null;
    }

    protected Resources.Theme getThemeImpl(Context context) {
        return null;
    }

    protected void setThemeImpl(Context context, int theme) {}

    protected Context createConfigurationContextImpl(Context context) {
        return null;
    }

    protected boolean isEnabledImpl() {
        return false;
    }

    protected void initCustomResourcesImpl(Context context) {}

    protected void maybeRecordResourceMetricsImpl() {}
}
