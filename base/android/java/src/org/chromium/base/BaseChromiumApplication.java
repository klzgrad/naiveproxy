// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.os.Bundle;
import android.view.Window;

import org.chromium.base.annotations.SuppressFBWarnings;
import org.chromium.base.multidex.ChromiumMultiDexInstaller;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/**
 * Basic application functionality that should be shared among all browser applications.
 */
public class BaseChromiumApplication extends Application {
    private static final String TAG = "base";
    private static final String TOOLBAR_CALLBACK_INTERNAL_WRAPPER_CLASS =
            "android.support.v7.internal.app.ToolbarActionBar$ToolbarCallbackWrapper";
    // In builds using the --use_unpublished_apis flag, the ToolbarActionBar class name does not
    // include the "internal" package.
    private static final String TOOLBAR_CALLBACK_WRAPPER_CLASS =
            "android.support.v7.app.ToolbarActionBar$ToolbarCallbackWrapper";
    private final boolean mShouldInitializeApplicationStatusTracking;

    public BaseChromiumApplication() {
        this(true);
    }

    protected BaseChromiumApplication(boolean shouldInitializeApplicationStatusTracking) {
        mShouldInitializeApplicationStatusTracking = shouldInitializeApplicationStatusTracking;
    }

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(base);
        assert getBaseContext() != null;
        checkAppBeingReplaced();
        if (BuildConfig.isMultidexEnabled()) {
            ChromiumMultiDexInstaller.install(this);
        }
    }

    /**
     * Interface to be implemented by listeners for window focus events.
     */
    public interface WindowFocusChangedListener {
        /**
         * Called when the window focus changes for {@code activity}.
         * @param activity The {@link Activity} that has a window focus changed event.
         * @param hasFocus Whether or not {@code activity} gained or lost focus.
         */
        public void onWindowFocusChanged(Activity activity, boolean hasFocus);
    }

    private ObserverList<WindowFocusChangedListener> mWindowFocusListeners =
            new ObserverList<>();

    /**
     * Intercepts calls to an existing Window.Callback. Most invocations are passed on directly
     * to the composed Window.Callback but enables intercepting/manipulating others.
     *
     * This is used to relay window focus changes throughout the app and remedy a bug in the
     * appcompat library.
     */
    private class WindowCallbackProxy implements InvocationHandler {
        private final Window.Callback mCallback;
        private final Activity mActivity;

        public WindowCallbackProxy(Activity activity, Window.Callback callback) {
            mCallback = callback;
            mActivity = activity;
        }

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            if (method.getName().equals("onWindowFocusChanged") && args.length == 1
                    && args[0] instanceof Boolean) {
                onWindowFocusChanged((boolean) args[0]);
                return null;
            } else {
                try {
                    return method.invoke(mCallback, args);
                } catch (InvocationTargetException e) {
                    // Special-case for when a method is not defined on the underlying
                    // Window.Callback object. Because we're using a Proxy to forward all method
                    // calls, this breaks the Android framework's handling for apps built against
                    // an older SDK. The framework expects an AbstractMethodError but due to
                    // reflection it becomes wrapped inside an InvocationTargetException. Undo the
                    // wrapping to signal the framework accordingly.
                    if (e.getCause() instanceof AbstractMethodError) {
                        throw e.getCause();
                    }
                    throw e;
                }
            }
        }

        public void onWindowFocusChanged(boolean hasFocus) {
            mCallback.onWindowFocusChanged(hasFocus);

            for (WindowFocusChangedListener listener : mWindowFocusListeners) {
                listener.onWindowFocusChanged(mActivity, hasFocus);
            }
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();

        if (mShouldInitializeApplicationStatusTracking) startTrackingApplicationStatus();
    }

    /**
     * Registers a listener to receive window focus updates on activities in this application.
     * @param listener Listener to receive window focus events.
     */
    public void registerWindowFocusChangedListener(WindowFocusChangedListener listener) {
        mWindowFocusListeners.addObserver(listener);
    }

    /**
     * Unregisters a listener from receiving window focus updates on activities in this application.
     * @param listener Listener that doesn't want to receive window focus events.
     */
    public void unregisterWindowFocusChangedListener(WindowFocusChangedListener listener) {
        mWindowFocusListeners.removeObserver(listener);
    }

    /** Initializes the {@link CommandLine}. */
    public void initCommandLine() {}

    /**
     * This must only be called for contexts whose application is a subclass of
     * {@link BaseChromiumApplication}.
     */
    @VisibleForTesting
    public static void initCommandLine(Context context) {
        ((BaseChromiumApplication) context.getApplicationContext()).initCommandLine();
    }

    /** Ensure this application object is not out-of-date. */
    @SuppressFBWarnings("DM_EXIT")
    private void checkAppBeingReplaced() {
        // During app update the old apk can still be triggered by broadcasts and spin up an
        // out-of-date application. Kill old applications in this bad state. See
        // http://crbug.com/658130 for more context and http://b.android.com/56296 for the bug.
        if (getResources() == null) {
            Log.e(TAG, "getResources() null, closing app.");
            System.exit(0);
        }
    }

    /** Register hooks and listeners to start tracking the application status. */
    private void startTrackingApplicationStatus() {
        ApplicationStatus.initialize(this);
        registerActivityLifecycleCallbacks(new ActivityLifecycleCallbacks() {
            @Override
            public void onActivityCreated(final Activity activity, Bundle savedInstanceState) {
                Window.Callback callback = activity.getWindow().getCallback();
                activity.getWindow().setCallback((Window.Callback) Proxy.newProxyInstance(
                        Window.Callback.class.getClassLoader(), new Class[] {Window.Callback.class},
                        new WindowCallbackProxy(activity, callback)));
            }

            @Override
            public void onActivityDestroyed(Activity activity) {
                if (BuildConfig.DCHECK_IS_ON) {
                    assert (Proxy.isProxyClass(activity.getWindow().getCallback().getClass())
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_WRAPPER_CLASS)
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_INTERNAL_WRAPPER_CLASS));
                }
            }

            @Override
            public void onActivityPaused(Activity activity) {
                if (BuildConfig.DCHECK_IS_ON) {
                    assert (Proxy.isProxyClass(activity.getWindow().getCallback().getClass())
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_WRAPPER_CLASS)
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_INTERNAL_WRAPPER_CLASS));
                }
            }

            @Override
            public void onActivityResumed(Activity activity) {
                if (BuildConfig.DCHECK_IS_ON) {
                    assert (Proxy.isProxyClass(activity.getWindow().getCallback().getClass())
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_WRAPPER_CLASS)
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_INTERNAL_WRAPPER_CLASS));
                }
            }

            @Override
            public void onActivitySaveInstanceState(Activity activity, Bundle outState) {
                if (BuildConfig.DCHECK_IS_ON) {
                    assert (Proxy.isProxyClass(activity.getWindow().getCallback().getClass())
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_WRAPPER_CLASS)
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_INTERNAL_WRAPPER_CLASS));
                }
            }

            @Override
            public void onActivityStarted(Activity activity) {
                if (BuildConfig.DCHECK_IS_ON) {
                    assert (Proxy.isProxyClass(activity.getWindow().getCallback().getClass())
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_WRAPPER_CLASS)
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_INTERNAL_WRAPPER_CLASS));
                }
            }

            @Override
            public void onActivityStopped(Activity activity) {
                if (BuildConfig.DCHECK_IS_ON) {
                    assert (Proxy.isProxyClass(activity.getWindow().getCallback().getClass())
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_WRAPPER_CLASS)
                            || activity.getWindow().getCallback().getClass().getName().equals(
                                    TOOLBAR_CALLBACK_INTERNAL_WRAPPER_CLASS));
                }
            }
        });
    }
}
