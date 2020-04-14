// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.url;

import android.os.SystemClock;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.metrics.RecordHistogram;

/**
 * A Java wrapper for GURL, Chromium's URL parsing library.
 *
 * This class is safe to use during startup, but will block on the native library being sufficiently
 * loaded to use native GURL (and will not wait for content initialization). In practice it's very
 * unlikely that this will actually block startup unless used extremely early, in which case you
 * should probably seek an alternative solution to using GURL.
 *
 * The design of this class avoids destruction/finalization by caching all values necessary to
 * reconstruct a GURL in Java, allowing it to be much faster in the common case and easier to use.
 */
@JNINamespace("url")
@MainDex
public class GURL {
    // TODO(https://crbug.com/1039841): Right now we return a new String with each request for a
    //      GURL component other than the spec itself. Should we cache return Strings (as
    //      WeakReference?) so that callers can share String memory?
    private String mSpec;
    private boolean mIsValid;
    private Parsed mParsed;

    /**
     * Create a new GURL.
     *
     * @param uri The string URI representation to parse into a GURL.
     */
    public GURL(String uri) {
        long time = SystemClock.elapsedRealtime();
        LibraryLoader.getInstance().ensureMainDexInitialized();
        RecordHistogram.recordTimesHistogram("Startup.Android.GURLEnsureMainDexInitialized",
                SystemClock.elapsedRealtime() - time);
        GURLJni.get().init(uri, this);
    }

    protected GURL() {}

    @CalledByNative
    private void init(String spec, boolean isValid, Parsed parsed) {
        mSpec = spec;
        // Ensure that the spec only contains US-ASCII or the parsed indices will be wrong.
        assert mSpec.matches("\\A\\p{ASCII}*\\z");
        mIsValid = isValid;
        mParsed = parsed;
    }

    @CalledByNative
    private long toNativeGURL() {
        return GURLJni.get().createNative(mSpec, mIsValid, mParsed.toNativeParsed());
    }

    /**
     * See native GURL::is_valid().
     */
    public boolean isValid() {
        return mIsValid;
    }

    /**
     * See native GURL::spec().
     */
    public String getSpec() {
        if (isValid() || mSpec.isEmpty()) return mSpec;
        assert false : "Trying to get the spec of an invalid URL!";
        return "";
    }

    /**
     * See native GURL::possibly_invalid_spec().
     */
    public String getPossiblyInvalidSpec() {
        return mSpec;
    }

    private String getComponent(int begin, int length) {
        if (length <= 0) return "";
        return mSpec.substring(begin, begin + length);
    }

    /**
     * See native GURL::scheme().
     */
    public String getScheme() {
        return getComponent(mParsed.mSchemeBegin, mParsed.mSchemeLength);
    }

    /**
     * See native GURL::username().
     */
    public String getUsername() {
        return getComponent(mParsed.mUsernameBegin, mParsed.mUsernameLength);
    }

    /**
     * See native GURL::password().
     */
    public String getPassword() {
        return getComponent(mParsed.mPasswordBegin, mParsed.mPasswordLength);
    }

    /**
     * See native GURL::host().
     */
    public String getHost() {
        return getComponent(mParsed.mHostBegin, mParsed.mHostLength);
    }

    /**
     * See native GURL::port().
     *
     * Note: Do not convert this to an integer yourself. See native GURL::IntPort().
     */
    public String getPort() {
        return getComponent(mParsed.mPortBegin, mParsed.mPortLength);
    }

    /**
     * See native GURL::path().
     */
    public String getPath() {
        return getComponent(mParsed.mPathBegin, mParsed.mPathLength);
    }

    /**
     * See native GURL::query().
     */
    public String getQuery() {
        return getComponent(mParsed.mQueryBegin, mParsed.mQueryLength);
    }

    /**
     * See native GURL::ref().
     */
    public String getRef() {
        return getComponent(mParsed.mRefBegin, mParsed.mRefLength);
    }

    /**
     * @return Whether the GURL is the empty String.
     */
    public boolean isEmpty() {
        return mSpec.isEmpty();
    }

    /**
     * See native GURL::GetOrigin().
     */
    public GURL getOrigin() {
        GURL target = new GURL();
        getOriginInternal(target);
        return target;
    }

    protected void getOriginInternal(GURL target) {
        GURLJni.get().getOrigin(mSpec, mIsValid, mParsed.toNativeParsed(), target);
    }

    @Override
    public final int hashCode() {
        return mSpec.hashCode();
    }

    @Override
    public final boolean equals(Object other) {
        if (other == this) return true;
        if (!(other instanceof GURL)) return false;
        return mSpec.equals(((GURL) other).mSpec);
    }

    @NativeMethods
    interface Natives {
        /**
         * Initializes the provided |target| by parsing the provided |uri|.
         */
        void init(String uri, GURL target);

        /**
         * Reconstructs the native GURL for this Java GURL and initializes |target| with its Origin.
         */
        void getOrigin(String spec, boolean isValid, long nativeParsed, GURL target);

        /**
         * Reconstructs the native GURL for this Java GURL, returning its native pointer.
         */
        long createNative(String spec, boolean isValid, long nativeParsed);
    }
}
