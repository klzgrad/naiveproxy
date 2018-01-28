// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.build;

import android.content.Context;
import android.content.res.AssetManager;
import android.content.res.Resources;

/**
 * All Java targets that require android have dependence on this class. Add methods that do not
 * require Android to {@link BuildHooks}.
 *
 * This class provides hooks needed when bytecode rewriting. Static convenience methods are used to
 * minimize the amount of code required to be manually generated when bytecode rewriting.
 *
 * This class contains default implementations for all methods and is used when no other
 * implementation is supplied to an android_apk target (via build_hooks_android_impl_deps).
 */
public abstract class BuildHooksAndroid {
    private static final BuildHooksAndroidImpl sInstance = new BuildHooksAndroidImpl();

    public static Resources getResources(Context context) {
        return sInstance.getResourcesImpl(context);
    }

    protected abstract Resources getResourcesImpl(Context context);

    public static AssetManager getAssets(Context context) {
        return sInstance.getAssetsImpl(context);
    }

    protected abstract AssetManager getAssetsImpl(Context context);

    public static Resources.Theme getTheme(Context context) {
        return sInstance.getThemeImpl(context);
    }

    protected abstract Resources.Theme getThemeImpl(Context context);

    public static void setTheme(Context context, int theme) {
        sInstance.setThemeImpl(context, theme);
    }

    protected abstract void setThemeImpl(Context context, int theme);

    public static Context createConfigurationContext(Context context) {
        return sInstance.createConfigurationContextImpl(context);
    }

    protected abstract Context createConfigurationContextImpl(Context context);

    public static boolean isEnabled() {
        return sInstance.isEnabledImpl();
    }

    protected abstract boolean isEnabledImpl();

    public static void initCustomResources(Context context) {
        sInstance.initCustomResourcesImpl(context);
    }

    protected abstract void initCustomResourcesImpl(Context context);

    /**
     * Record custom resources related UMA. Requires native library to be loaded.
     */
    public static void maybeRecordResourceMetrics() {
        sInstance.maybeRecordResourceMetricsImpl();
    }

    protected abstract void maybeRecordResourceMetricsImpl();
}