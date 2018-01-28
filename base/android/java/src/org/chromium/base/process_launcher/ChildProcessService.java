// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

/**
 * This is the base class for child services; the embedding application should contain
 * ProcessService0, 1.. etc subclasses that provide the concrete service entry points, so it can
 * connect to more than one distinct process (i.e. one process per service number, up to limit of
 * N).
 * The embedding application must declare these service instances in the application section
 * of its AndroidManifest.xml, first with some meta-data describing the services:
 *     <meta-data android:name="org.chromium.test_app.SERVICES_NAME"
 *           android:value="org.chromium.test_app.ProcessService"/>
 * and then N entries of the form:
 *     <service android:name="org.chromium.test_app.ProcessServiceX"
 *              android:process=":processX" />
 *
 * Subclasses must also provide a delegate in this class constructor. That delegate is responsible
 * for loading native libraries and running the main entry point of the service.
 */
public abstract class ChildProcessService extends Service {
    private final ChildProcessServiceImpl mChildProcessServiceImpl;

    protected ChildProcessService(ChildProcessServiceDelegate delegate) {
        mChildProcessServiceImpl = new ChildProcessServiceImpl(delegate);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mChildProcessServiceImpl.create(getApplicationContext(), getApplicationContext());
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        mChildProcessServiceImpl.destroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        // We call stopSelf() to request that this service be stopped as soon as the client
        // unbinds. Otherwise the system may keep it around and available for a reconnect. The
        // child processes do not currently support reconnect; they must be initialized from
        // scratch every time.
        stopSelf();
        return mChildProcessServiceImpl.bind(intent, -1);
    }
}
