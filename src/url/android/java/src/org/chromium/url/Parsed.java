// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.url;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.annotations.NativeMethods;

/**
 * A java wrapper for Parsed, GURL's internal parsed URI representation.
 */
@MainDex
@JNINamespace("url")
/* package */ class Parsed {
    /* package */ final int mSchemeBegin;
    /* package */ final int mSchemeLength;
    /* package */ final int mUsernameBegin;
    /* package */ final int mUsernameLength;
    /* package */ final int mPasswordBegin;
    /* package */ final int mPasswordLength;
    /* package */ final int mHostBegin;
    /* package */ final int mHostLength;
    /* package */ final int mPortBegin;
    /* package */ final int mPortLength;
    /* package */ final int mPathBegin;
    /* package */ final int mPathLength;
    /* package */ final int mQueryBegin;
    /* package */ final int mQueryLength;
    /* package */ final int mRefBegin;
    /* package */ final int mRefLength;
    private final Parsed mInnerUrl;
    private final boolean mPotentiallyDanglingMarkup;

    @CalledByNative
    private Parsed(int schemeBegin, int schemeLength, int usernameBegin, int usernameLength,
            int passwordBegin, int passwordLength, int hostBegin, int hostLength, int portBegin,
            int portLength, int pathBegin, int pathLength, int queryBegin, int queryLength,
            int refBegin, int refLength, boolean potentiallyDanglingMarkup, Parsed innerUrl) {
        mSchemeBegin = schemeBegin;
        mSchemeLength = schemeLength;
        mUsernameBegin = usernameBegin;
        mUsernameLength = usernameLength;
        mPasswordBegin = passwordBegin;
        mPasswordLength = passwordLength;
        mHostBegin = hostBegin;
        mHostLength = hostLength;
        mPortBegin = portBegin;
        mPortLength = portLength;
        mPathBegin = pathBegin;
        mPathLength = pathLength;
        mQueryBegin = queryBegin;
        mQueryLength = queryLength;
        mRefBegin = refBegin;
        mRefLength = refLength;
        mPotentiallyDanglingMarkup = potentiallyDanglingMarkup;
        mInnerUrl = innerUrl;
    }

    /* package */ long toNativeParsed() {
        long inner = 0;
        if (mInnerUrl != null) {
            inner = mInnerUrl.toNativeParsed();
        }
        return ParsedJni.get().createNative(mSchemeBegin, mSchemeLength, mUsernameBegin,
                mUsernameLength, mPasswordBegin, mPasswordLength, mHostBegin, mHostLength,
                mPortBegin, mPortLength, mPathBegin, mPathLength, mQueryBegin, mQueryLength,
                mRefBegin, mRefLength, mPotentiallyDanglingMarkup, inner);
    }

    @NativeMethods
    interface Natives {
        /**
         * Create and return the pointer to a native Parsed.
         */
        long createNative(int schemeBegin, int schemeLength, int usernameBegin, int usernameLength,
                int passwordBegin, int passwordLength, int hostBegin, int hostLength, int portBegin,
                int portLength, int pathBegin, int pathLength, int queryBegin, int queryLength,
                int refBegin, int refLength, boolean potentiallyDanglingMarkup, long innerUrl);
    }
}
