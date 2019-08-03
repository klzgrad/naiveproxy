// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.task;

import android.os.Process;
import android.support.annotation.Nullable;
import android.util.Pair;

import org.chromium.base.LifetimeAssert;
import org.chromium.base.TraceEvent;
import org.chromium.base.annotations.JNINamespace;

import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;

import javax.annotation.concurrent.GuardedBy;

/**
 * Implementation of the abstract class {@link TaskRunnerImpl}. Uses AsyncTasks until
 * native APIs are available.
 */
@JNINamespace("base")
public class TaskRunnerImpl implements TaskRunner {
    private final TaskTraits mTaskTraits;
    private final String mTraceEvent;
    private final @TaskRunnerType int mTaskRunnerType;
    protected final Object mLock = new Object();
    @GuardedBy("mLock")
    protected long mNativeTaskRunnerAndroid;
    protected final Runnable mRunPreNativeTaskClosure = this::runPreNativeTask;
    @GuardedBy("mLock")
    private boolean mIsDestroying;
    private final LifetimeAssert mLifetimeAssert = LifetimeAssert.create(this);

    @Nullable
    protected LinkedList<Runnable> mPreNativeTasks = new LinkedList<>();
    @Nullable
    protected List<Pair<Runnable, Long>> mPreNativeDelayedTasks = new ArrayList<>();

    /**
     * @param traits The TaskTraits associated with this TaskRunnerImpl.
     */
    TaskRunnerImpl(TaskTraits traits) {
        this(traits, "TaskRunnerImpl", TaskRunnerType.BASE);
    }

    /**
     * @param traits The TaskTraits associated with this TaskRunnerImpl.
     * @param traceCategory Specifies which subclass is this instance for logging purposes.
     * @param taskRunnerType Specifies which subclass is this instance for initialising the correct
     *         native scheduler.
     */
    protected TaskRunnerImpl(
            TaskTraits traits, String traceCategory, @TaskRunnerType int taskRunnerType) {
        mTaskTraits = traits;
        mTraceEvent = traceCategory + ".PreNativeTask.run";
        mTaskRunnerType = taskRunnerType;
        if (!PostTask.registerPreNativeTaskRunnerLocked(this)) initNativeTaskRunner();
    }

    @Override
    public void destroy() {
        synchronized (mLock) {
            LifetimeAssert.setSafeToGc(mLifetimeAssert, true);
            mIsDestroying = true;
            destroyInternal();
        }
    }

    @GuardedBy("mLock")
    protected void destroyInternal() {
        if (mNativeTaskRunnerAndroid != 0) nativeDestroy(mNativeTaskRunnerAndroid);
        mNativeTaskRunnerAndroid = 0;
    }

    @Override
    public void disableLifetimeCheck() {
        LifetimeAssert.setSafeToGc(mLifetimeAssert, true);
    }

    @Override
    public void postTask(Runnable task) {
        postDelayedTask(task, 0);
    }

    @Override
    public void postDelayedTask(Runnable task, long delay) {
        synchronized (mLock) {
            assert !mIsDestroying;
            if (mPreNativeTasks == null) {
                postDelayedTaskToNative(task, delay);
                return;
            }
            // We don't expect a whole lot of these, if that changes consider pooling them.
            // If a task is scheduled for immediate execution, we post it on the
            // pre-native task runner. Tasks scheduled to run with a delay will
            // wait until the native task runner is initialised.
            if (delay == 0) {
                mPreNativeTasks.add(task);
                schedulePreNativeTask();
            } else {
                Pair<Runnable, Long> preNativeDelayedTask = new Pair<>(task, delay);
                mPreNativeDelayedTasks.add(preNativeDelayedTask);
            }
        }
    }

    /**
     * Must be overridden in subclasses, schedules a call to runPreNativeTask() at an appropriate
     * time.
     */
    protected void schedulePreNativeTask() {
        PostTask.getPrenativeThreadPoolExecutor().execute(mRunPreNativeTaskClosure);
    }

    /**
     * Runs a single task and returns when its finished.
     */
    protected void runPreNativeTask() {
        try (TraceEvent te = TraceEvent.scoped(mTraceEvent)) {
            Runnable task;
            synchronized (mLock) {
                if (mPreNativeTasks == null) return;
                task = mPreNativeTasks.poll();
            }
            switch (mTaskTraits.mPriority) {
                case TaskPriority.USER_VISIBLE:
                    Process.setThreadPriority(Process.THREAD_PRIORITY_DEFAULT);
                    break;
                case TaskPriority.HIGHEST:
                    Process.setThreadPriority(Process.THREAD_PRIORITY_MORE_FAVORABLE);
                    break;
                default:
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
                    break;
            }
            task.run();
        }
    }

    /**
     * Instructs the TaskRunner to initialize the native TaskRunner and migrate any tasks over to
     * it.
     */
    @Override
    public void initNativeTaskRunner() {
        synchronized (mLock) {
            initNativeTaskRunnerInternal();
            migratePreNativeTasksToNative();
        }
    }

    @GuardedBy("mLock")
    protected void initNativeTaskRunnerInternal() {
        if (mNativeTaskRunnerAndroid == 0) {
            mNativeTaskRunnerAndroid = nativeInit(mTaskRunnerType,
                    mTaskTraits.mPrioritySetExplicitly, mTaskTraits.mPriority,
                    mTaskTraits.mMayBlock, mTaskTraits.mUseThreadPool, mTaskTraits.mExtensionId,
                    mTaskTraits.mExtensionData);
        }
    }

    @GuardedBy("mLock")
    protected void migratePreNativeTasksToNative() {
        if (mPreNativeTasks != null) {
            for (Runnable task : mPreNativeTasks) {
                postDelayedTaskToNative(task, 0);
            }
            for (Pair<Runnable, Long> task : mPreNativeDelayedTasks) {
                postDelayedTaskToNative(task.first, task.second);
            }
            mPreNativeTasks = null;
            mPreNativeDelayedTasks = null;
        }
    }

    @GuardedBy("mLock")
    protected void postDelayedTaskToNative(Runnable r, long delay) {
        nativePostDelayedTask(mNativeTaskRunnerAndroid, r, delay);
    }

    // NB due to Proguard obfuscation it's easiest to pass the traits via arguments.
    private native long nativeInit(@TaskRunnerType int taskRunnerType,
            boolean prioritySetExplicitly, int priority, boolean mayBlock, boolean useThreadPool,
            byte extensionId, byte[] extensionData);
    private native void nativeDestroy(long nativeTaskRunnerAndroid);
    private native void nativePostDelayedTask(
            long nativeTaskRunnerAndroid, Runnable task, long delay);
    protected native boolean nativeBelongsToCurrentThread(long nativeTaskRunnerAndroid);
}
