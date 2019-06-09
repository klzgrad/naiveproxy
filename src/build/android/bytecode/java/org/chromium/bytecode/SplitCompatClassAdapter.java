// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.bytecode;

import static org.objectweb.asm.Opcodes.ACC_PROTECTED;
import static org.objectweb.asm.Opcodes.ALOAD;
import static org.objectweb.asm.Opcodes.INVOKESPECIAL;
import static org.objectweb.asm.Opcodes.INVOKESTATIC;
import static org.objectweb.asm.Opcodes.RETURN;

import static org.chromium.bytecode.TypeUtils.CONTEXT;
import static org.chromium.bytecode.TypeUtils.VOID;

import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

import java.util.Set;

/**
 * A ClassVisitor for injecting ModuleInstaller.initActivity(activity) method call
 * into Activity's attachBaseContext() method. The goal is to eventually invoke
 * SplitCompat.install() method if running with the binary that has bundle support
 * enabled. This needs to happen for activities that were not built with SplitCompat
 * support.
 */
class SplitCompatClassAdapter extends ClassVisitor {
    private static final String ANDROID_APP_ACTIVITY_CLASS_NAME = "android/app/Activity";
    private static final String ATTACH_BASE_CONTEXT_METHOD_NAME = "attachBaseContext";
    private static final String ATTACH_BASE_CONTEXT_DESCRIPTOR =
            TypeUtils.getMethodDescriptor(VOID, CONTEXT);

    private static final String MODULE_INSTALLER_CLASS_NAME =
            "org/chromium/components/module_installer/ModuleInstaller";
    private static final String INIT_ACTIVITY_METHOD_NAME = "initActivity";
    private static final String INIT_ACTIVITY_DESCRIPTOR =
            TypeUtils.getMethodDescriptor(VOID, CONTEXT);

    private boolean mShouldTransform;

    private Set<String> mClassNames;

    private ClassLoader mClassLoader;

    /**
     * Creates instance of SplitCompatClassAdapter.
     *
     * @param visitor
     * @param classNames Names of classes into which the attachBaseContext method will be
     *         injected. Currently, we'll only consider classes for bytecode rewriting only if
     *         they inherit directly from android.app.Activity & not already contain
     *         attachBaseContext method.
     * @param classLoader
     */
    SplitCompatClassAdapter(ClassVisitor visitor, Set<String> classNames, ClassLoader classLoader) {
        super(Opcodes.ASM5, visitor);

        mShouldTransform = false;
        mClassNames = classNames;
        mClassLoader = classLoader;
    }

    @Override
    public void visit(int version, int access, String name, String signature, String superName,
            String[] interfaces) {
        super.visit(version, access, name, signature, superName, interfaces);

        if (mClassNames.contains(name)) {
            if (!isSubclassOfActivity(name)) {
                throw new RuntimeException(name
                        + " should be transformed but does not inherit from android.app.Activity");
            }

            mShouldTransform = true;
        }
    }

    @Override
    public MethodVisitor visitMethod(
            int access, String name, String descriptor, String signature, String[] exceptions) {
        // Check if current method matches attachBaseContext & we're supposed to emit code - if so,
        // fail.
        if (mShouldTransform && name.equals(ATTACH_BASE_CONTEXT_METHOD_NAME)) {
            throw new RuntimeException(ATTACH_BASE_CONTEXT_METHOD_NAME + " method already exists");
        }

        return super.visitMethod(access, name, descriptor, signature, exceptions);
    }

    @Override
    public void visitEnd() {
        if (mShouldTransform) {
            // If we reached this place, it means we're rewriting a class that inherits from
            // Activity and there was no exception thrown due to existence of attachBaseContext
            // method - emit code.
            emitAttachBaseContext();
        }

        super.visitEnd();
    }

    /**
     * Generates:
     *
     * <pre>
     * protected void attachBaseContext(Context base) {
     *     super.attachBaseContext(base);
     *     ModuleInstaller.initActivity(this);
     * }
     * </pre>
     */
    private void emitAttachBaseContext() {
        MethodVisitor mv = super.visitMethod(ACC_PROTECTED, ATTACH_BASE_CONTEXT_METHOD_NAME,
                ATTACH_BASE_CONTEXT_DESCRIPTOR, null, null);
        mv.visitCode();
        mv.visitVarInsn(ALOAD, 0); // load "this" on stack
        mv.visitVarInsn(ALOAD, 1); // load first method parameter on stack (Context)
        mv.visitMethodInsn(INVOKESPECIAL, ANDROID_APP_ACTIVITY_CLASS_NAME,
                ATTACH_BASE_CONTEXT_METHOD_NAME,
                ATTACH_BASE_CONTEXT_DESCRIPTOR); // invoke super's attach base context
        mv.visitVarInsn(ALOAD, 0); // load "this" on stack
        mv.visitMethodInsn(INVOKESTATIC, MODULE_INSTALLER_CLASS_NAME, INIT_ACTIVITY_METHOD_NAME,
                INIT_ACTIVITY_DESCRIPTOR);
        mv.visitInsn(RETURN);
        mv.visitMaxs(2, 2); // max stack size - 2, max locals - 2
        mv.visitEnd();
    }

    /**
     * Checks whether passed in class inherits from android.app.Activity.
     * @param name Name of the class to be checked.
     * @return true if class inherits from android.app.Activity, false otherwise.
     */
    private boolean isSubclassOfActivity(String name) {
        Class<?> activityClass = loadClass(ANDROID_APP_ACTIVITY_CLASS_NAME);
        Class<?> candidateClass = loadClass(name);
        return activityClass.isAssignableFrom(candidateClass);
    }

    private Class<?> loadClass(String className) {
        try {
            return mClassLoader.loadClass(className.replace('/', '.'));
        } catch (ClassNotFoundException e) {
            throw new RuntimeException(e);
        }
    }
}
