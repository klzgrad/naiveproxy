// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.task;

import android.support.annotation.Nullable;

import java.util.Arrays;

/**
 * TaskTraits are metadata that influence how the TaskSecheduler deals with that task.
 * E.g. the trait can directly or indirectly control task prioritization.
 */
public class TaskTraits {
    // Keep in sync with base::TaskTraitsExtensionStorage:: kInvalidExtensionId
    public static final byte INVALID_EXTENSION_ID = 0;

    // Keep in sync with base::TaskTraitsExtensionStorage::kMaxExtensionId
    public static final int MAX_EXTENSION_ID = 4;

    // Keep in sync with base::TaskTraitsExtensionStorage::kStorageSize
    public static final int EXTENSION_STORAGE_SIZE = 8;

    // Convenience variables explicitly specifying common priorities

    // This task will only be scheduled when machine resources are available. Once
    // running, it may be descheduled if higher priority work arrives (in this
    // process or another) and its running on a non-critical thread. This is the
    // lowest possible priority.
    public static final TaskTraits BEST_EFFORT =
            new TaskTraits().taskPriority(TaskPriority.BEST_EFFORT);

    // This is a lowest-priority task which may block, for example non-urgent
    // logging or deletion of temporary files as clean-up.
    public static final TaskTraits BEST_EFFORT_MAY_BLOCK =
            new TaskTraits().taskPriority(TaskPriority.BEST_EFFORT).mayBlock(true);

    // This task affects UI or responsiveness of future user interactions. It is
    // not an immediate response to a user interaction. Most tasks are likely to
    // have this priority.
    // Examples:
    // - Updating the UI to reflect progress on a long task.
    // - Loading data that might be shown in the UI after a future user
    //   interaction.
    public static final TaskTraits USER_VISIBLE =
            new TaskTraits().taskPriority(TaskPriority.USER_VISIBLE);

    // This task affects UI immediately after a user interaction.
    // Example: Generating data shown in the UI immediately after a click.
    // Is is different from the mMayBlock property in that it doesn't contribute
    // to the creation of additional thread pool threads. This is the highest
    // possible priority.
    public static final TaskTraits USER_BLOCKING =
            new TaskTraits().taskPriority(TaskPriority.USER_BLOCKING);

    public TaskTraits() {}

    private TaskTraits(TaskTraits other) {
        mPrioritySetExplicitly = other.mPrioritySetExplicitly;
        mPriority = other.mPriority;
        mMayBlock = other.mMayBlock;
        mExtensionId = other.mExtensionId;
        mExtensionData = other.mExtensionData;
    }

    public TaskTraits(byte extensionId, byte[] extensionData) {
        mExtensionId = extensionId;
        mExtensionData = extensionData;
    }

    public TaskTraits taskPriority(int taskPriority) {
        TaskTraits taskTraits = new TaskTraits(this);
        taskTraits.mPrioritySetExplicitly = true;
        taskTraits.mPriority = taskPriority;
        return taskTraits;
    }

    /**
     * Tasks with this trait may block. This includes but is not limited to tasks that wait on
     * synchronous file I/O operations: read or write a file from disk, interact with a pipe or a
     * socket, rename or delete a file, enumerate files in a directory, etc. This trait isn't
     * required for the mere use of locks. The thread pool uses this property to work out if
     * additional threads are required.
     */
    public TaskTraits mayBlock(boolean mayBlock) {
        TaskTraits taskTraits = new TaskTraits(this);
        taskTraits.mMayBlock = mayBlock;
        return taskTraits;
    }

    // For convenience of the JNI code, we use primitive types only.
    // Note shutdown behavior is not supported on android.
    boolean mPrioritySetExplicitly;
    int mPriority = TaskPriority.USER_VISIBLE;
    boolean mMayBlock;
    byte mExtensionId = INVALID_EXTENSION_ID;
    byte mExtensionData[];

    /**
     * @return true if this task is using some TaskTraits extension.
     */
    public boolean hasExtension() {
        return mExtensionId != INVALID_EXTENSION_ID;
    }

    /**
     * Tries to extract the extension for the given descriptor from this traits.
     *
     * @return Extension instance or null if the traits do not contain the requested extension
     */
    public <Extension> Extension getExtension(TaskTraitsExtensionDescriptor<Extension> descriptor) {
        if (mExtensionId == descriptor.getId()) {
            return descriptor.fromSerializedData(mExtensionData);
        } else {
            return null;
        }
    }

    public <Extension> TaskTraits withExtension(
            TaskTraitsExtensionDescriptor<Extension> descriptor, Extension extension) {
        int id = descriptor.getId();
        byte[] data = descriptor.toSerializedData(extension);
        assert id > INVALID_EXTENSION_ID && id <= MAX_EXTENSION_ID;
        assert data.length <= EXTENSION_STORAGE_SIZE;

        TaskTraits taskTraits = new TaskTraits(this);
        taskTraits.mExtensionId = (byte) id;
        taskTraits.mExtensionData = data;
        return taskTraits;
    }

    @Override
    public boolean equals(@Nullable Object object) {
        if (object == this) {
            return true;
        } else if (object instanceof TaskTraits) {
            TaskTraits other = (TaskTraits) object;
            return mPrioritySetExplicitly == other.mPrioritySetExplicitly
                    && mPriority == other.mPriority && mExtensionId == other.mExtensionId
                    && Arrays.equals(mExtensionData, other.mExtensionData);
        } else {
            return false;
        }
    }

    @Override
    public int hashCode() {
        int hash = 31;
        hash = 37 * hash + (mPrioritySetExplicitly ? 0 : 1);
        hash = 37 * hash + mPriority;
        hash = 37 * hash + (mMayBlock ? 0 : 1);
        hash = 37 * hash + (int) mExtensionId;
        hash = 37 * hash + Arrays.hashCode(mExtensionData);
        return hash;
    }
}
