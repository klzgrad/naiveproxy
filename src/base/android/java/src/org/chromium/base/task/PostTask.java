// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.task;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.util.Collections;
import java.util.Set;
import java.util.WeakHashMap;

/**
 * Java interface to the native chromium scheduler.  Note tasks can be posted before native
 * initialization, but task prioritization is extremely limited. Once the native scheduler
 * is ready, tasks will be migrated over.
 */
@JNINamespace("base")
public class PostTask {
    private static final Object sLock = new Object();
    private static Set<TaskRunner> sPreNativeTaskRunners =
            Collections.newSetFromMap(new WeakHashMap<TaskRunner, Boolean>());

    private static final TaskExecutor sTaskExecutors[] = getInitialTaskExecutors();

    private static TaskExecutor[] getInitialTaskExecutors() {
        TaskExecutor taskExecutors[] = new TaskExecutor[TaskTraits.MAX_EXTENSION_ID + 1];
        taskExecutors[0] = new DefaultTaskExecutor();
        return taskExecutors;
    }

    /**
     * @param traits The TaskTraits that describe the desired TaskRunner.
     * @return The TaskRunner for the specified TaskTraits.
     */
    public static TaskRunner createTaskRunner(TaskTraits taskTraits) {
        synchronized (sLock) {
            return getTaskExecutorForTraits(taskTraits).createTaskRunner(taskTraits);
        }
    }

    /**
     * @param traits The TaskTraits that describe the desired TaskRunner.
     * @return The TaskRunner for the specified TaskTraits.
     */
    public static SequencedTaskRunner createSequencedTaskRunner(TaskTraits taskTraits) {
        synchronized (sLock) {
            return getTaskExecutorForTraits(taskTraits).createSequencedTaskRunner(taskTraits);
        }
    }

    /**
     *
     * @param traits The TaskTraits that describe the desired TaskRunner.
     * @return The TaskRunner for the specified TaskTraits.
     */
    public static SingleThreadTaskRunner createSingleThreadTaskRunner(TaskTraits taskTraits) {
        synchronized (sLock) {
            return getTaskExecutorForTraits(taskTraits).createSingleThreadTaskRunner(taskTraits);
        }
    }

    /**
     * @param taskTraits The TaskTraits that describe the desired TaskRunner.
     * @param task The task to be run with the specified traits.
     */
    public static void postTask(TaskTraits taskTraits, Runnable task) {
        postDelayedTask(taskTraits, task, 0);
    }

    /**
     * @param taskTraits The TaskTraits that describe the desired TaskRunner.
     * @param task The task to be run with the specified traits.
     * @param delay The delay in milliseconds before the task can be run.
     */
    public static void postDelayedTask(TaskTraits taskTraits, Runnable task, long delay) {
        synchronized (sLock) {
            if (sPreNativeTaskRunners != null) {
                getTaskExecutorForTraits(taskTraits).postDelayedTask(taskTraits, task, delay);
            } else {
                nativePostDelayedTask(taskTraits.mPrioritySetExplicitly, taskTraits.mPriority,
                        taskTraits.mMayBlock, taskTraits.mExtensionId, taskTraits.mExtensionData,
                        task, delay);
            }
        }
    }

    /**
     * Registers a TaskExecutor, this must be called before any other usages of this API.
     *
     * @param extensionId The id associated with the TaskExecutor.
     * @param taskExecutor The TaskExecutor to be registered. Must not equal zero.
     */
    public static void registerTaskExecutor(byte extensionId, TaskExecutor taskExecutor) {
        synchronized (sLock) {
            assert extensionId != 0;
            assert extensionId <= TaskTraits.MAX_EXTENSION_ID;
            assert sTaskExecutors[extensionId] == null;
            sTaskExecutors[extensionId] = taskExecutor;
        }
    }

    /**
     * Called by every TaskRunner on its creation, attempts to register this
     * TaskRunner as pre-native, unless the native scheduler has been
     * initialised already, and informs the caller about the outcome. Called
     * only when sLock has already been acquired.
     *
     * @param taskRunner The TaskRunner to be registered.
     * @return If the taskRunner got registered as pre-native.
     */
    static boolean registerPreNativeTaskRunnerLocked(TaskRunner taskRunner) {
        if (sPreNativeTaskRunners != null) {
            sPreNativeTaskRunners.add(taskRunner);
            return true;
        }
        return false;
    }

    private static TaskExecutor getTaskExecutorForTraits(TaskTraits traits) {
        return sTaskExecutors[traits.mExtensionId];
    }

    @CalledByNative
    private static void onNativeTaskSchedulerReady() {
        synchronized (sLock) {
            for (TaskRunner taskRunner : sPreNativeTaskRunners) {
                taskRunner.initNativeTaskRunner();
            }
            sPreNativeTaskRunners = null;
        }
    }

    // This is here to make C++ tests work.
    @CalledByNative
    private static void onNativeTaskSchedulerShutdown() {
        synchronized (sLock) {
            sPreNativeTaskRunners =
                    Collections.newSetFromMap(new WeakHashMap<TaskRunner, Boolean>());
        }
    }

    private static native void nativePostDelayedTask(boolean prioritySetExplicitly, int priority,
            boolean mayBlock, byte extensionId, byte[] extensionData, Runnable task, long delay);
}
