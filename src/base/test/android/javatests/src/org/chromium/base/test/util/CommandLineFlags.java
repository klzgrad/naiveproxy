// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import android.text.TextUtils;

import org.junit.Assert;
import org.junit.Rule;

import org.chromium.base.CommandLine;
import org.chromium.base.CommandLineInitUtil;
import org.chromium.base.test.BaseTestResult.PreTestHook;

import java.lang.annotation.ElementType;
import java.lang.annotation.Inherited;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.AnnotatedElement;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Provides annotations related to command-line flag handling.
 *
 * Uses of these annotations on a derived class will take precedence over uses on its base classes,
 * so a derived class can add a command-line flag that a base class has removed (or vice versa).
 * Similarly, uses of these annotations on a test method will take precedence over uses on the
 * containing class.
 * <p>
 * These annonations may also be used on Junit4 Rule classes and on their base classes. Note,
 * however that the annotation processor only looks at the declared type of the Rule, not its actual
 * type, so in, for example:
 *
 * <pre>
 *     &#64Rule
 *     TestRule mRule = new ChromeActivityTestRule();
 * </pre>
 *
 * will only look for CommandLineFlags annotations on TestRule, not for CommandLineFlags annotations
 * on ChromeActivityTestRule.
 * <p>
 * In addition a rule may not remove flags added by an independently invoked rule, although it may
 * remove flags added by its base classes.
 * <p>
 * Uses of these annotations on the test class or methods take precedence over uses on Rule classes.
 * <p>
 * Note that this class should never be instantiated.
 */
public final class CommandLineFlags {
    private static final String DISABLE_FEATURES = "disable-features";
    private static final String ENABLE_FEATURES = "enable-features";

    /**
     * Adds command-line flags to the {@link org.chromium.base.CommandLine} for this test.
     */
    @Inherited
    @Retention(RetentionPolicy.RUNTIME)
    @Target({ElementType.METHOD, ElementType.TYPE})
    public @interface Add {
        String[] value();
    }

    /**
     * Removes command-line flags from the {@link org.chromium.base.CommandLine} from this test.
     *
     * Note that this can only remove flags added via {@link Add} above.
     */
    @Inherited
    @Retention(RetentionPolicy.RUNTIME)
    @Target({ElementType.METHOD, ElementType.TYPE})
    public @interface Remove {
        String[] value();
    }

    /**
     * Sets up the CommandLine with the appropriate flags.
     *
     * This will add the difference of the sets of flags specified by {@link CommandLineFlags.Add}
     * and {@link CommandLineFlags.Remove} to the {@link org.chromium.base.CommandLine}. Note that
     * trying to remove a flag set externally, i.e. by the command-line flags file, will not work.
     */
    public static void setUp(AnnotatedElement element) {
        CommandLine.reset();
        CommandLineInitUtil.initCommandLine(getTestCmdLineFile());
        Set<String> enableFeatures = new HashSet<String>(getFeatureValues(ENABLE_FEATURES));
        Set<String> disableFeatures = new HashSet<String>(getFeatureValues(DISABLE_FEATURES));
        Set<String> flags = getFlags(element);
        for (String flag : flags) {
            String[] parsedFlags = flag.split("=", 2);
            if (parsedFlags.length == 1) {
                CommandLine.getInstance().appendSwitch(flag);
            } else if (ENABLE_FEATURES.equals(parsedFlags[0])) {
                // We collect enable/disable features flags separately and aggregate them because
                // they may be specified multiple times, in which case the values will trample each
                // other.
                Collections.addAll(enableFeatures, parsedFlags[1].split(","));
            } else if (DISABLE_FEATURES.equals(parsedFlags[0])) {
                Collections.addAll(disableFeatures, parsedFlags[1].split(","));
            } else {
                CommandLine.getInstance().appendSwitchWithValue(parsedFlags[0], parsedFlags[1]);
            }
        }

        if (enableFeatures.size() > 0) {
            CommandLine.getInstance().appendSwitchWithValue(
                    ENABLE_FEATURES, TextUtils.join(",", enableFeatures));
        }
        if (disableFeatures.size() > 0) {
            CommandLine.getInstance().appendSwitchWithValue(
                    DISABLE_FEATURES, TextUtils.join(",", disableFeatures));
        }
    }

    private static Set<String> getFlags(AnnotatedElement type) {
        Set<String> rule_flags = new HashSet<>();
        updateFlagsForElement(type, rule_flags);
        return rule_flags;
    }

    private static void updateFlagsForElement(AnnotatedElement element, Set<String> flags) {
        if (element instanceof Class<?>) {
            // Get flags from rules within the class.
            for (Field field : ((Class<?>) element).getFields()) {
                if (field.isAnnotationPresent(Rule.class)) {
                    // The order in which fields are returned is undefined, so, for consistency,
                    // a rule must not remove a flag added by a different rule. Ensure this by
                    // initially getting the flags into a new set.
                    Set<String> rule_flags = getFlags(field.getType());
                    flags.addAll(rule_flags);
                }
            }
            for (Method method : ((Class<?>) element).getMethods()) {
                if (method.isAnnotationPresent(Rule.class)) {
                    // The order in which methods are returned is undefined, so, for consistency,
                    // a rule must not remove a flag added by a different rule. Ensure this by
                    // initially getting the flags into a new set.
                    Set<String> rule_flags = getFlags(method.getReturnType());
                    flags.addAll(rule_flags);
                }
            }
        }

        // Add the flags from the parent. Override any flags defined by the rules.
        AnnotatedElement parent = (element instanceof Method)
                ? ((Method) element).getDeclaringClass()
                : ((Class<?>) element).getSuperclass();
        if (parent != null) updateFlagsForElement(parent, flags);

        // Flags on the element itself override all other flag sources.
        if (element.isAnnotationPresent(CommandLineFlags.Add.class)) {
            flags.addAll(
                    Arrays.asList(element.getAnnotation(CommandLineFlags.Add.class).value()));
        }

        if (element.isAnnotationPresent(CommandLineFlags.Remove.class)) {
            List<String> flagsToRemove =
                    Arrays.asList(element.getAnnotation(CommandLineFlags.Remove.class).value());
            for (String flagToRemove : flagsToRemove) {
                // If your test fails here, you have tried to remove a command-line flag via
                // CommandLineFlags.Remove that was loaded into CommandLine via something other
                // than CommandLineFlags.Add (probably the command-line flag file).
                Assert.assertFalse("Unable to remove command-line flag \"" + flagToRemove + "\".",
                        CommandLine.getInstance().hasSwitch(flagToRemove));
            }
            flags.removeAll(flagsToRemove);
        }
    }

    private static List<String> getFeatureValues(String flag) {
        String value = CommandLine.getInstance().getSwitchValue(flag);
        if (value == null) return new ArrayList<>();
        return Arrays.asList(value.split(","));
    }

    private CommandLineFlags() {
        throw new AssertionError("CommandLineFlags is a non-instantiable class");
    }

    public static PreTestHook getRegistrationHook() {
        return (targetContext, testMethod) -> CommandLineFlags.setUp(testMethod);
    }

    public static String getTestCmdLineFile() {
        return "test-cmdline-file";
    }
}
