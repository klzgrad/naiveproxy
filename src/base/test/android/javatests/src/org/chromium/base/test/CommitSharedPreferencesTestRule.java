// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test;

import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;

import org.chromium.base.ContextUtils;

class CommitSharedPreferencesTestRule implements TestRule {
    @Override
    public Statement apply(Statement statement, Description description) {
        return new Statement() {
            @Override
            public void evaluate() throws Throwable {
                // Clear app's SharedPreferences before each test to reduce flakiness.
                // See https://crbug.com/908174, ttps://crbug.com/902774.
                ContextUtils.getAppSharedPreferences().edit().clear().commit();
                try {
                    statement.evaluate();
                } finally {
                    // Some disk writes to update SharedPreferences may still be in progress if
                    // apply() was used after editing. Commit these changes to SharedPreferences
                    // before reporting the test as finished. See https://crbug.com/916717.
                    ContextUtils.getAppSharedPreferences().edit().commit();
                }
            }
        };
    }
}
