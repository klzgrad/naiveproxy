// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import android.annotation.TargetApi;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.Handler;
import android.os.Process;
import android.os.UserHandle;

import java.lang.reflect.Method;

/**
 * Class of static helper methods to call Context.bindService variants.
 */
final class BindService {
    private static Method sBindServiceAsUserMethod;

    // Note that handler is not guaranteed to be used, and client still need to correctly handle
    // callbacks on the UI thread.
    static boolean doBindService(Context context, Intent intent, ServiceConnection connection,
            int flags, Handler handler) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
            return bindServiceByCall(context, intent, connection, flags);
        }

        try {
            return bindServiceByReflection(context, intent, connection, flags, handler);
        } catch (ReflectiveOperationException e) {
            return bindServiceByCall(context, intent, connection, flags);
        }
    }

    private static boolean bindServiceByCall(
            Context context, Intent intent, ServiceConnection connection, int flags) {
        return context.bindService(intent, connection, flags);
    }

    @TargetApi(Build.VERSION_CODES.N)
    private static boolean bindServiceByReflection(Context context, Intent intent,
            ServiceConnection connection, int flags, Handler handler)
            throws ReflectiveOperationException {
        if (sBindServiceAsUserMethod == null) {
            sBindServiceAsUserMethod =
                    Context.class.getDeclaredMethod("bindServiceAsUser", Intent.class,
                            ServiceConnection.class, int.class, Handler.class, UserHandle.class);
        }
        // No need for null checks or worry about infinite looping here. Otherwise a regular calls
        // into the ContextWrapper would lead to problems as well.
        while (context instanceof ContextWrapper) {
            context = ((ContextWrapper) context).getBaseContext();
        }
        return (Boolean) sBindServiceAsUserMethod.invoke(
                context, intent, connection, flags, handler, Process.myUserHandle());
    }

    private BindService() {}
}
