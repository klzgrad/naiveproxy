// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.task;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * Implementation of the abstract class {@link SequencedTaskRunner}. Uses AsyncTasks until
 * native APIs are available.
 */
public class SequencedTaskRunnerImpl extends TaskRunnerImpl implements SequencedTaskRunner {
    private AtomicInteger mPendingTasks = new AtomicInteger();

    /**
     * @param traits The TaskTraits associated with this SequencedTaskRunnerImpl.
     */
    SequencedTaskRunnerImpl(TaskTraits traits) {
        super(traits, "SequencedTaskRunnerImpl", TaskRunnerType.SEQUENCED);
    }

    @Override
    protected void schedulePreNativeTask() {
        if (mPendingTasks.getAndIncrement() == 0) {
            super.schedulePreNativeTask();
        }
    }

    @Override
    protected void runPreNativeTask() {
        super.runPreNativeTask();
        if (mPendingTasks.decrementAndGet() > 0) {
            super.schedulePreNativeTask();
        }
    }
}
