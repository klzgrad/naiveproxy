/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.perfetto.sdk;

import android.annotation.SuppressLint;
import android.os.Bundle;
import android.text.format.Formatter;
import android.util.Log;
import android.widget.TextView;
import android.app.Activity;

import java.io.File;

@SuppressLint("SetTextI18n")
public class MainActivity extends Activity {

    private static final String TAG = MainActivity.class.getSimpleName();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        File perfettoOutput = new File(getFilesDir(), "from_java_example.pftrace");
        String perfettoOutputPath = perfettoOutput.getAbsolutePath();
        Log.i(TAG, "perfettoOutputPath: " + perfettoOutputPath);

        TextView tv = (TextView) findViewById(R.id.sample_text);
        int perfettoResult = PerfettoExampleWrapper.doRunPerfettoMain(perfettoOutputPath);

        String outputFileSize = Formatter.formatShortFileSize(this, perfettoOutput.length());

        String message = "Perfetto result (expected result: 0): " + perfettoResult
                + ", output file size (expected size ~1.3kB): " + outputFileSize + ".";
        Log.i(TAG, message);
        tv.setText(message);
    }

}
