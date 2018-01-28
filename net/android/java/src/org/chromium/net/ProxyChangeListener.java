// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.Proxy;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;

import org.chromium.base.BuildConfig;
import org.chromium.base.ContextUtils;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeClassQualifiedName;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * This class partners with native ProxyConfigServiceAndroid to listen for
 * proxy change notifications from Android.
 */
@JNINamespace("net")
public class ProxyChangeListener {
    private static final String TAG = "ProxyChangeListener";
    private static boolean sEnabled = true;

    private final Looper mLooper;
    private final Handler mHandler;

    private long mNativePtr;
    private ProxyReceiver mProxyReceiver;
    private Delegate mDelegate;

    private static class ProxyConfig {
        public ProxyConfig(String host, int port, String pacUrl, String[] exclusionList) {
            mHost = host;
            mPort = port;
            mPacUrl = pacUrl;
            mExclusionList = exclusionList;
        }
        public final String mHost;
        public final int mPort;
        public final String mPacUrl;
        public final String[] mExclusionList;
    }

    /**
     * The delegate for ProxyChangeListener. Use for testing.
     */
    public interface Delegate {
        public void proxySettingsChanged();
    }

    private ProxyChangeListener() {
        mLooper = Looper.myLooper();
        mHandler = new Handler(mLooper);
    }

    public static void setEnabled(boolean enabled) {
        sEnabled = enabled;
    }

    public void setDelegateForTesting(Delegate delegate) {
        mDelegate = delegate;
    }

    @CalledByNative
    public static ProxyChangeListener create() {
        return new ProxyChangeListener();
    }

    @CalledByNative
    public static String getProperty(String property) {
        return System.getProperty(property);
    }

    @CalledByNative
    public void start(long nativePtr) {
        assertOnThread();
        assert mNativePtr == 0;
        mNativePtr = nativePtr;
        registerReceiver();
    }

    @CalledByNative
    public void stop() {
        assertOnThread();
        mNativePtr = 0;
        unregisterReceiver();
    }

    private class ProxyReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, final Intent intent) {
            if (intent.getAction().equals(Proxy.PROXY_CHANGE_ACTION)) {
                runOnThread(new Runnable() {
                    @Override
                    public void run() {
                        proxySettingsChanged(ProxyReceiver.this, extractNewProxy(intent));
                    }
                });
            }
        }

        // Extract a ProxyConfig object from the supplied Intent's extra data
        // bundle. The android.net.ProxyProperties class is not exported from
        // the Android SDK, so we have to use reflection to get at it and invoke
        // methods on it. If we fail, return an empty proxy config (meaning
        // 'direct').
        // TODO(sgurun): once android.net.ProxyInfo is public, rewrite this.
        private ProxyConfig extractNewProxy(Intent intent) {
            try {
                final String getHostName = "getHost";
                final String getPortName = "getPort";
                final String getPacFileUrl = "getPacFileUrl";
                final String getExclusionList = "getExclusionList";
                String className;
                String proxyInfo;
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
                    className = "android.net.ProxyProperties";
                    proxyInfo = "proxy";
                } else {
                    className = "android.net.ProxyInfo";
                    proxyInfo = "android.intent.extra.PROXY_INFO";
                }

                Object props = intent.getExtras().get(proxyInfo);
                if (props == null) {
                    return null;
                }

                Class<?> cls = Class.forName(className);
                Method getHostMethod = cls.getDeclaredMethod(getHostName);
                Method getPortMethod = cls.getDeclaredMethod(getPortName);
                Method getExclusionListMethod = cls.getDeclaredMethod(getExclusionList);

                String host = (String) getHostMethod.invoke(props);
                int port = (Integer) getPortMethod.invoke(props);

                String[] exclusionList;
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
                    String s = (String) getExclusionListMethod.invoke(props);
                    exclusionList = s.split(",");
                } else {
                    exclusionList = (String[]) getExclusionListMethod.invoke(props);
                }
                // TODO(xunjieli): rewrite this once the API is public.
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT
                        && Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
                    Method getPacFileUrlMethod = cls.getDeclaredMethod(getPacFileUrl);
                    String pacFileUrl = (String) getPacFileUrlMethod.invoke(props);
                    if (!TextUtils.isEmpty(pacFileUrl)) {
                        return new ProxyConfig(host, port, pacFileUrl, exclusionList);
                    }
                } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    Method getPacFileUrlMethod = cls.getDeclaredMethod(getPacFileUrl);
                    Uri pacFileUrl = (Uri) getPacFileUrlMethod.invoke(props);
                    if (!Uri.EMPTY.equals(pacFileUrl)) {
                        return new ProxyConfig(host, port, pacFileUrl.toString(), exclusionList);
                    }
                }
                return new ProxyConfig(host, port, null, exclusionList);
            } catch (ClassNotFoundException ex) {
                Log.e(TAG, "Using no proxy configuration due to exception:" + ex);
                return null;
            } catch (NoSuchMethodException ex) {
                Log.e(TAG, "Using no proxy configuration due to exception:" + ex);
                return null;
            } catch (IllegalAccessException ex) {
                Log.e(TAG, "Using no proxy configuration due to exception:" + ex);
                return null;
            } catch (InvocationTargetException ex) {
                Log.e(TAG, "Using no proxy configuration due to exception:" + ex);
                return null;
            } catch (NullPointerException ex) {
                Log.e(TAG, "Using no proxy configuration due to exception:" + ex);
                return null;
            }
        }
    }

    private void proxySettingsChanged(ProxyReceiver proxyReceiver, ProxyConfig cfg) {
        if (!sEnabled
                // Once execution begins on the correct thread, make sure unregisterReceiver()
                // hasn't been called in the mean time. Ignore the changed signal if
                // unregisterReceiver() was called.
                || proxyReceiver != mProxyReceiver) {
            return;
        }
        if (mDelegate != null) {
            mDelegate.proxySettingsChanged();
        }
        if (mNativePtr == 0) {
            return;
        }
        // Note that this code currently runs on a MESSAGE_LOOP_UI thread, but
        // the C++ code must run the callbacks on the network thread.
        if (cfg != null) {
            nativeProxySettingsChangedTo(mNativePtr, cfg.mHost, cfg.mPort, cfg.mPacUrl,
                    cfg.mExclusionList);
        } else {
            nativeProxySettingsChanged(mNativePtr);
        }
    }

    private void registerReceiver() {
        if (mProxyReceiver != null) {
            return;
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(Proxy.PROXY_CHANGE_ACTION);
        mProxyReceiver = new ProxyReceiver();
        ContextUtils.getApplicationContext().registerReceiver(mProxyReceiver, filter);
    }

    private void unregisterReceiver() {
        if (mProxyReceiver == null) {
            return;
        }
        ContextUtils.getApplicationContext().unregisterReceiver(mProxyReceiver);
        mProxyReceiver = null;
    }

    private boolean onThread() {
        return mLooper == Looper.myLooper();
    }

    private void assertOnThread() {
        if (BuildConfig.DCHECK_IS_ON && !onThread()) {
            throw new IllegalStateException("Must be called on ProxyChangeListener thread.");
        }
    }

    private void runOnThread(Runnable r) {
        if (onThread()) {
            r.run();
        } else {
            mHandler.post(r);
        }
    }

    /**
     * See net/proxy/proxy_config_service_android.cc
     */
    @NativeClassQualifiedName("ProxyConfigServiceAndroid::JNIDelegate")
    private native void nativeProxySettingsChangedTo(long nativePtr,
                                                     String host,
                                                     int port,
                                                     String pacUrl,
                                                     String[] exclusionList);
    @NativeClassQualifiedName("ProxyConfigServiceAndroid::JNIDelegate")
    private native void nativeProxySettingsChanged(long nativePtr);
}
