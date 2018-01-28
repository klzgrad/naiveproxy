// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.library_loader;

import android.content.Context;

/**
 * This is interface to preload the native library before calling System.loadLibrary.
 */
public abstract class NativeLibraryPreloader {
    public abstract int loadLibrary(Context context);
}
