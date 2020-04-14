// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.Application;
import android.app.Application.ActivityLifecycleCallbacks;
import android.os.Bundle;
import android.view.ViewTreeObserver;

import androidx.annotation.AnyThread;
import androidx.annotation.MainThread;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import javax.annotation.concurrent.GuardedBy;

/**
 * Provides information about the current activity's status, and a way
 * to register / unregister listeners for state changes.
 */
@JNINamespace("base::android")
public class ApplicationStatus {
    private static class ActivityInfo {
        private int mStatus = ActivityState.DESTROYED;
        private ObserverList<ActivityStateListener> mListeners = new ObserverList<>();

        /**
         * @return The current {@link ActivityState} of the activity.
         */
        @ActivityState
        public int getStatus() {
            return mStatus;
        }

        /**
         * @param status The new {@link ActivityState} of the activity.
         */
        public void setStatus(@ActivityState int status) {
            mStatus = status;
        }

        /**
         * @return A list of {@link ActivityStateListener}s listening to this activity.
         */
        public ObserverList<ActivityStateListener> getListeners() {
            return mListeners;
        }
    }

    /**
     * A map of which observers listen to state changes from which {@link Activity}.
     */
    private static final Map<Activity, ActivityInfo> sActivityInfo =
            Collections.synchronizedMap(new HashMap<Activity, ActivityInfo>());

    @SuppressLint("SupportAnnotationUsage")
    @ApplicationState
    @GuardedBy("sActivityInfo")
    // The getStateForApplication() historically returned ApplicationState.HAS_DESTROYED_ACTIVITIES
    // when no activity has been observed.
    private static int sCurrentApplicationState = ApplicationState.UNKNOWN;

    /** Last activity that was shown (or null if none or it was destroyed). */
    @SuppressLint("StaticFieldLeak")
    private static Activity sActivity;

    /** A lazily initialized listener that forwards application state changes to native. */
    private static ApplicationStateListener sNativeApplicationStateListener;

    /**
     * A list of observers to be notified when any {@link Activity} has a state change.
     */
    private static final ObserverList<ActivityStateListener> sGeneralActivityStateListeners =
            new ObserverList<>();

    /**
     * A list of observers to be notified when the visibility state of this {@link Application}
     * changes.  See {@link #getStateForApplication()}.
     */
    private static final ObserverList<ApplicationStateListener> sApplicationStateListeners =
            new ObserverList<>();

    /**
     * A list of observers to be notified when the window focus changes.
     * See {@link #registerWindowFocusChangedListener}.
     */
    private static final ObserverList<WindowFocusChangedListener> sWindowFocusListeners =
            new ObserverList<>();

    /**
     * Interface to be implemented by listeners.
     */
    public interface ApplicationStateListener {
        /**
         * Called when the application's state changes.
         * @param newState The application state.
         */
        void onApplicationStateChange(@ApplicationState int newState);
    }

    /**
     * Interface to be implemented by listeners.
     */
    public interface ActivityStateListener {
        /**
         * Called when the activity's state changes.
         * @param activity The activity that had a state change.
         * @param newState New activity state.
         */
        void onActivityStateChange(Activity activity, @ActivityState int newState);
    }

    /**
     * Interface to be implemented by listeners for window focus events.
     */
    public interface WindowFocusChangedListener {
        /**
         * Called when the window focus changes for {@code activity}.
         * @param activity The {@link Activity} that has a window focus changed event.
         * @param hasFocus Whether or not {@code activity} gained or lost focus.
         */
        public void onWindowFocusChanged(Activity activity, boolean hasFocus);
    }

    private ApplicationStatus() {}

    /**
     * Registers a listener to receive window focus updates on activities in this application.
     * @param listener Listener to receive window focus events.
     */
    @MainThread
    public static void registerWindowFocusChangedListener(WindowFocusChangedListener listener) {
        assert isInitialized();
        sWindowFocusListeners.addObserver(listener);
    }

    /**
     * Unregisters a listener from receiving window focus updates on activities in this application.
     * @param listener Listener that doesn't want to receive window focus events.
     */
    @MainThread
    public static void unregisterWindowFocusChangedListener(WindowFocusChangedListener listener) {
        sWindowFocusListeners.removeObserver(listener);
    }

    /**
     * Monitors activity focus changes as a ViewTreeObserver.
     *
     * This is used to relay window focus changes throughout the app and remedy a bug in the
     * appcompat library.
     */
    private static class WindowFocusObserver
            implements ViewTreeObserver.OnWindowFocusChangeListener {
        private final Activity mActivity;

        public WindowFocusObserver(Activity activity) {
            mActivity = activity;
        }

        @Override
        public void onWindowFocusChanged(boolean hasFocus) {
            for (WindowFocusChangedListener listener : sWindowFocusListeners) {
                listener.onWindowFocusChanged(mActivity, hasFocus);
            }
        }
    }

    public static boolean isInitialized() {
        synchronized (sActivityInfo) {
            return sCurrentApplicationState != ApplicationState.UNKNOWN;
        }
    }

    /**
     * Initializes the activity status for a specified application.
     *
     * @param application The application whose status you wish to monitor.
     */
    @MainThread
    public static void initialize(Application application) {
        assert !isInitialized();
        synchronized (sActivityInfo) {
            sCurrentApplicationState = ApplicationState.HAS_DESTROYED_ACTIVITIES;
        }

        registerWindowFocusChangedListener(new WindowFocusChangedListener() {
            @Override
            public void onWindowFocusChanged(Activity activity, boolean hasFocus) {
                if (!hasFocus || activity == sActivity) return;

                int state = getStateForActivity(activity);

                if (state != ActivityState.DESTROYED && state != ActivityState.STOPPED) {
                    sActivity = activity;
                }

                // TODO(dtrainor): Notify of active activity change?
            }
        });

        application.registerActivityLifecycleCallbacks(new ActivityLifecycleCallbacks() {
            @Override
            public void onActivityCreated(final Activity activity, Bundle savedInstanceState) {
                onStateChange(activity, ActivityState.CREATED);
                activity.getWindow()
                        .getDecorView()
                        .getViewTreeObserver()
                        .addOnWindowFocusChangeListener(new WindowFocusObserver(activity));
            }

            @Override
            public void onActivityDestroyed(Activity activity) {
                onStateChange(activity, ActivityState.DESTROYED);
            }

            @Override
            public void onActivityPaused(Activity activity) {
                onStateChange(activity, ActivityState.PAUSED);
            }

            @Override
            public void onActivityResumed(Activity activity) {
                onStateChange(activity, ActivityState.RESUMED);
            }

            @Override
            public void onActivitySaveInstanceState(Activity activity, Bundle outState) {
            }

            @Override
            public void onActivityStarted(Activity activity) {
                onStateChange(activity, ActivityState.STARTED);
            }

            @Override
            public void onActivityStopped(Activity activity) {
                onStateChange(activity, ActivityState.STOPPED);
            }
        });
    }

    /**
     * Must be called by the main activity when it changes state.
     *
     * @param activity Current activity.
     * @param newState New state value.
     */
    private static void onStateChange(Activity activity, @ActivityState int newState) {
        if (activity == null) throw new IllegalArgumentException("null activity is not supported");

        if (sActivity == null
                || newState == ActivityState.CREATED
                || newState == ActivityState.RESUMED
                || newState == ActivityState.STARTED) {
            sActivity = activity;
        }

        int oldApplicationState = getStateForApplication();
        ActivityInfo info;

        synchronized (sActivityInfo) {
            if (newState == ActivityState.CREATED) {
                assert !sActivityInfo.containsKey(activity);
                sActivityInfo.put(activity, new ActivityInfo());
            }

            info = sActivityInfo.get(activity);
            info.setStatus(newState);

            // Remove before calling listeners so that isEveryActivityDestroyed() returns false when
            // this was the last activity.
            if (newState == ActivityState.DESTROYED) {
                sActivityInfo.remove(activity);
                if (activity == sActivity) sActivity = null;
            }

            sCurrentApplicationState = determineApplicationStateLocked();
        }

        // Notify all state observers that are specifically listening to this activity.
        for (ActivityStateListener listener : info.getListeners()) {
            listener.onActivityStateChange(activity, newState);
        }

        // Notify all state observers that are listening globally for all activity state
        // changes.
        for (ActivityStateListener listener : sGeneralActivityStateListeners) {
            listener.onActivityStateChange(activity, newState);
        }

        int applicationState = getStateForApplication();
        if (applicationState != oldApplicationState) {
            for (ApplicationStateListener listener : sApplicationStateListeners) {
                listener.onApplicationStateChange(applicationState);
            }
        }
    }

    /**
     * Testing method to update the state of the specified activity.
     */
    @VisibleForTesting
    @MainThread
    public static void onStateChangeForTesting(Activity activity, int newState) {
        onStateChange(activity, newState);
    }

    /**
     * @return The most recent focused {@link Activity} tracked by this class.  Being focused means
     *         out of all the activities tracked here, it has most recently gained window focus.
     */
    @MainThread
    public static Activity getLastTrackedFocusedActivity() {
        return sActivity;
    }

    /**
     * @return A {@link List} of all non-destroyed {@link Activity}s.
     */
    @AnyThread
    public static List<Activity> getRunningActivities() {
        assert isInitialized();
        synchronized (sActivityInfo) {
            return new ArrayList<>(sActivityInfo.keySet());
        }
    }

    /**
     * Query the state for a given activity.  If the activity is not being tracked, this will
     * return {@link ActivityState#DESTROYED}.
     *
     * <p>
     * Please note that Chrome can have multiple activities running simultaneously.  Please also
     * look at {@link #getStateForApplication()} for more details.
     *
     * <p>
     * When relying on this method, be familiar with the expected life cycle state
     * transitions:
     * <a href="http://developer.android.com/guide/components/activities.html#Lifecycle">
     *   Activity Lifecycle
     * </a>
     *
     * <p>
     * During activity transitions (activity B launching in front of activity A), A will completely
     * paused before the creation of activity B begins.
     *
     * <p>
     * A basic flow for activity A starting, followed by activity B being opened and then closed:
     * <ul>
     *   <li> -- Starting Activity A --
     *   <li> Activity A - ActivityState.CREATED
     *   <li> Activity A - ActivityState.STARTED
     *   <li> Activity A - ActivityState.RESUMED
     *   <li> -- Starting Activity B --
     *   <li> Activity A - ActivityState.PAUSED
     *   <li> Activity B - ActivityState.CREATED
     *   <li> Activity B - ActivityState.STARTED
     *   <li> Activity B - ActivityState.RESUMED
     *   <li> Activity A - ActivityState.STOPPED
     *   <li> -- Closing Activity B, Activity A regaining focus --
     *   <li> Activity B - ActivityState.PAUSED
     *   <li> Activity A - ActivityState.STARTED
     *   <li> Activity A - ActivityState.RESUMED
     *   <li> Activity B - ActivityState.STOPPED
     *   <li> Activity B - ActivityState.DESTROYED
     * </ul>
     *
     * @param activity The activity whose state is to be returned.
     * @return The state of the specified activity (see {@link ActivityState}).
     */
    @ActivityState
    @AnyThread
    public static int getStateForActivity(@Nullable Activity activity) {
        assert isInitialized();
        if (activity == null) return ActivityState.DESTROYED;
        ActivityInfo info = sActivityInfo.get(activity);
        return info != null ? info.getStatus() : ActivityState.DESTROYED;
    }

    /**
     * @return The state of the application (see {@link ApplicationState}).
     */
    @AnyThread
    @ApplicationState
    @CalledByNative
    public static int getStateForApplication() {
        synchronized (sActivityInfo) {
            return sCurrentApplicationState;
        }
    }

    /**
     * Checks whether or not any Activity in this Application is visible to the user.  Note that
     * this includes the PAUSED state, which can happen when the Activity is temporarily covered
     * by another Activity's Fragment (e.g.).
     * @return Whether any Activity under this Application is visible.
     */
    @AnyThread
    @CalledByNative
    public static boolean hasVisibleActivities() {
        assert isInitialized();
        int state = getStateForApplication();
        return state == ApplicationState.HAS_RUNNING_ACTIVITIES
                || state == ApplicationState.HAS_PAUSED_ACTIVITIES;
    }

    /**
     * Checks to see if there are any active Activity instances being watched by ApplicationStatus.
     * @return True if all Activities have been destroyed.
     */
    @AnyThread
    public static boolean isEveryActivityDestroyed() {
        assert isInitialized();
        return sActivityInfo.isEmpty();
    }

    /**
     * Registers the given listener to receive state changes for all activities.
     * @param listener Listener to receive state changes.
     */
    @MainThread
    public static void registerStateListenerForAllActivities(ActivityStateListener listener) {
        assert isInitialized();
        sGeneralActivityStateListeners.addObserver(listener);
    }

    /**
     * Registers the given listener to receive state changes for {@code activity}.  After a call to
     * {@link ActivityStateListener#onActivityStateChange(Activity, int)} with
     * {@link ActivityState#DESTROYED} all listeners associated with that particular
     * {@link Activity} are removed.
     * @param listener Listener to receive state changes.
     * @param activity Activity to track or {@code null} to track all activities.
     */
    @MainThread
    @SuppressLint("NewApi")
    public static void registerStateListenerForActivity(
            ActivityStateListener listener, Activity activity) {
        assert isInitialized();
        assert activity != null;

        ActivityInfo info = sActivityInfo.get(activity);
        assert info.getStatus() != ActivityState.DESTROYED;
        info.getListeners().addObserver(listener);
    }

    /**
     * Unregisters the given listener from receiving activity state changes.
     * @param listener Listener that doesn't want to receive state changes.
     */
    @MainThread
    public static void unregisterActivityStateListener(ActivityStateListener listener) {
        sGeneralActivityStateListeners.removeObserver(listener);

        // Loop through all observer lists for all activities and remove the listener.
        synchronized (sActivityInfo) {
            for (ActivityInfo info : sActivityInfo.values()) {
                info.getListeners().removeObserver(listener);
            }
        }
    }

    /**
     * Registers the given listener to receive state changes for the application.
     * @param listener Listener to receive state state changes.
     */
    @MainThread
    public static void registerApplicationStateListener(ApplicationStateListener listener) {
        sApplicationStateListeners.addObserver(listener);
    }

    /**
     * Unregisters the given listener from receiving state changes.
     * @param listener Listener that doesn't want to receive state changes.
     */
    @MainThread
    public static void unregisterApplicationStateListener(ApplicationStateListener listener) {
        sApplicationStateListeners.removeObserver(listener);
    }

    /**
     * Robolectric JUnit tests create a new application between each test, while all the context
     * in static classes isn't reset. This function allows to reset the application status to avoid
     * being in a dirty state.
     */
    @MainThread
    public static void destroyForJUnitTests() {
        synchronized (sActivityInfo) {
            sApplicationStateListeners.clear();
            sGeneralActivityStateListeners.clear();
            sActivityInfo.clear();
            sWindowFocusListeners.clear();
            sCurrentApplicationState = ApplicationState.UNKNOWN;
            sActivity = null;
            sNativeApplicationStateListener = null;
        }
    }

    /**
     * Registers the single thread-safe native activity status listener.
     * This handles the case where the caller is not on the main thread.
     * Note that this is used by a leaky singleton object from the native
     * side, hence lifecycle management is greatly simplified.
     */
    @CalledByNative
    private static void registerThreadSafeNativeApplicationStateListener() {
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (sNativeApplicationStateListener != null) return;

                sNativeApplicationStateListener = new ApplicationStateListener() {
                    @Override
                    public void onApplicationStateChange(int newState) {
                        ApplicationStatusJni.get().onApplicationStateChange(newState);
                    }
                };
                registerApplicationStateListener(sNativeApplicationStateListener);
            }
        });
    }

    /**
     * Determines the current application state as defined by {@link ApplicationState}.  This will
     * loop over all the activities and check their state to determine what the general application
     * state should be.
     * @return HAS_RUNNING_ACTIVITIES if any activity is not paused, stopped, or destroyed.
     *         HAS_PAUSED_ACTIVITIES if none are running and one is paused.
     *         HAS_STOPPED_ACTIVITIES if none are running/paused and one is stopped.
     *         HAS_DESTROYED_ACTIVITIES if none are running/paused/stopped.
     */
    @ApplicationState
    @GuardedBy("sActivityInfo")
    private static int determineApplicationStateLocked() {
        boolean hasPausedActivity = false;
        boolean hasStoppedActivity = false;

        for (ActivityInfo info : sActivityInfo.values()) {
            int state = info.getStatus();
            if (state != ActivityState.PAUSED && state != ActivityState.STOPPED
                    && state != ActivityState.DESTROYED) {
                return ApplicationState.HAS_RUNNING_ACTIVITIES;
            } else if (state == ActivityState.PAUSED) {
                hasPausedActivity = true;
            } else if (state == ActivityState.STOPPED) {
                hasStoppedActivity = true;
            }
        }

        if (hasPausedActivity) return ApplicationState.HAS_PAUSED_ACTIVITIES;
        if (hasStoppedActivity) return ApplicationState.HAS_STOPPED_ACTIVITIES;
        return ApplicationState.HAS_DESTROYED_ACTIVITIES;
    }

    @NativeMethods
    interface Natives {
        // Called to notify the native side of state changes.
        // IMPORTANT: This is always called on the main thread!
        void onApplicationStateChange(@ApplicationState int newState);
    }
}
