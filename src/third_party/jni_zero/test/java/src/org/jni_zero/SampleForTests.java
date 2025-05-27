// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.jni_zero;

import android.graphics.Rect;

import org.jni_zero.internal.Nullable;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * This class serves as a reference test for the bindings generator, and as example documentation
 * for how to use the jni generator.
 *
 * <p>The C++ counter-part is sample_for_tests.cc.
 *
 * <p>jni_zero/BUILD.gn has a sample_jni_apk target that uses these files to create a test app that
 * exercises some basic JNI when the app is opened.
 *
 * <p>All comments are informational only, and are ignored by the jni generator.
 *
 * <p>This JNINamespace annotation indicates that all native methods should be generated inside this
 * namespace, including the native class that this object binds to.
 */
@JNINamespace("jni_zero::tests")
@NullMarked
class SampleForTests {
    // Classes can store their C++ pointer counterpart as an int that is normally initialized by
    // calling out a SampleForTestsJni.get().init() function. Replace "CPPClass" with your
    // particular class name!
    long mNativeCPPObject;

    // You can define methods and attributes on the java class just like any other.
    // Methods without the @CalledByNative annotation won't be exposed to JNI.
    public SampleForTests() {}

    public void startExample() {
        // Calls C++ Init(...) method and holds a pointer to the C++ class.
        mNativeCPPObject = SampleForTestsJni.get().init(this, "myParam", new byte[0], null, null);
    }

    public void doStuff() {
        // This will call CPPClass::Method() using nativePtr as a pointer to the object. This must
        // be done to:
        // * avoid leaks.
        // * using finalizers are not allowed to destroy the cpp class.
        SampleForTestsJni.get().method(mNativeCPPObject, this, new String[] {"test"});
    }

    // Just a comment to ensure we aren't reading comments:
    // private native void thisShouldNotExist();

    public void finishExample() {
        // We're done, so let's destroy nativePtr object.
        SampleForTestsJni.get().destroy(mNativeCPPObject, this, new byte[0]);
    }

    // ---------------------------------------------------------------------------------------------
    // The following methods demonstrate exporting Java methods for invocation from C++ code.
    // Java functions are mapping into C global functions by prefixing the method name with
    // "Java_<Class>_"
    // This is triggered by the @CalledByNative annotation; the methods may be named as you wish.

    // Exported to C++ as:
    // Java_SampleForTests_javaMethod(JNIEnv* env, jobject caller, jint foo, jint bar)
    // Typically the C++ code would have obtained the jobject via the Init() call described above.
    @CalledByNative
    public int javaMethod(int jcaller, int ret) {
        return 0;
    }

    // Exported to C++ as Java_SampleForTests_staticJavaMethod(JNIEnv* env)
    // Note no jobject argument, as it is static.
    @CalledByNative
    public static boolean staticJavaMethod() throws Exception {
        return true;
    }

    // We do not want to include androidx annotations but this is basically how
    // the real annotation looks like.
    @interface AnnotationWithNamedNonStringParam {
        int otherwise() default PRIVATE;

        int PRIVATE = 2;
    }

    @CalledByNative
    @AnnotationWithNamedNonStringParam(otherwise = AnnotationWithNamedNonStringParam.PRIVATE)
    public boolean methodWithAnnotationParamAssignment() {
        return false;
    }

    // No prefix, so this method is package private. It will still be exported.
    @CalledByNative
    void packagePrivateJavaMethod() {}

    // Method signature with generics in params.
    @CalledByNative
    public void methodWithGenericParams(
            Map<String, Map<String, String>> env, LinkedList<Integer> bar) {}

    // Constructors will be exported to C++ as:
    // Java_SampleForTests_Constructor(JNIEnv* env, jint foo, jint bar)
    @CalledByNative
    public SampleForTests(int foo, int bar) {}

    // Tests @JniType for @CalledByNative methods.
    @CalledByNative
    @JniType("std::string")
    public String getFirstString(
            @JniType("std::vector<const char*>") String[] array,
            @JniType("const char*") String finalArg) {
        return array[0];
    }

    // Note the "Unchecked" suffix. By default, @CalledByNative will always generate bindings that
    // call CheckException(). With "@CalledByNativeUnchecked", the client C++ code is responsible to
    // call ClearException() and act as appropriate.
    // See more details at the "@CalledByNativeUnchecked" annotation.
    @CalledByNativeUnchecked
    void methodThatThrowsException() throws Exception {}

    // The generator is not confused by inline comments:
    // @CalledByNative void thisShouldNotAppearInTheOutput();
    // @CalledByNativeUnchecked public static void neitherShouldThis(int foo);

    /**
     * The generator is not confused by block comments:
     * @CalledByNative void thisShouldNotAppearInTheOutputEither();
     * @CalledByNativeUnchecked public static void andDefinitelyNotThis(int foo);
     */

    // String constants that look like comments don't confuse the generator:
    private String mArrgh = "*/*";

    private @interface SomeAnnotation {}
    private @interface AnotherAnnotation {}

    // The generator is not confused by @Annotated parameters.
    @CalledByNative
    @JniType("std::vector<int32_t>")
    int[] jniTypesAndAnnotations(
            @SomeAnnotation @JniType("MyEnum") int foo,
            final @SomeAnnotation @JniType("std::vector<int32_t>") int[] bar,
            @SomeAnnotation final int baz,
            @SomeAnnotation @JniType("long") final @AnotherAnnotation long bat) {
        return bar;
    }

    @CalledByNative
    @JniType("std::vector")
    static Collection<SampleForTests> listTest1(
            @JniType("std::vector<std::string>") List<String> items) {
        return Collections.emptyList();
    }

    @CalledByNative
    static @JniType("std::map<std::string, std::string>") @Nullable Map<String, String> mapTest1(
            @JniType("std::map<std::string, std::string>") Map<String, String> arg0) {
        return arg0;
    }

    // ---------------------------------------------------------------------------------------------
    // Java fields which are accessed from C++ code only must be annotated with @AccessedByNative to
    // prevent them being eliminated when unreferenced code is stripped.
    @AccessedByNative private int mJavaField;

    // This "struct" will be created by the native side using |createInnerStructA|,
    // and used by the java-side somehow.
    // Note that |@CalledByNative| has to contain the inner class name.
    static class InnerStructA {
        private final long mLong;
        private final int mInt;
        private final String mString;

        private InnerStructA(long l, int i, String s) {
            mLong = l;
            mInt = i;
            mString = s;
        }

        @CalledByNative("InnerStructA")
        private static InnerStructA create(long l, int i, String s) {
            return new InnerStructA(l, i, s);
        }
    }

    private List<InnerStructA> mListInnerStructA = new ArrayList<InnerStructA>();

    @CalledByNative
    private SampleForTests.@Nullable InnerStructA addStructA(
            SampleForTests.@Nullable InnerStructA a) {
        // Called by the native side to append another element.
        mListInnerStructA.add(a);
        return null;
    }

    @CalledByNative
    private void iterateAndDoSomething() {
        Iterator<InnerStructA> it = mListInnerStructA.iterator();
        while (it.hasNext()) {
            InnerStructA element = it.next();
            // Now, do something with element.
        }
        // Done, clear the list.
        mListInnerStructA.clear();
    }

    // This "struct" will be created by the java side passed to native, which
    // will use its getters.
    // Note that |@CalledByNative| has to contain the inner class name.
    static class InnerStructB {
        private final long mKey;
        private final String mValue;

        private InnerStructB(long k, String v) {
            mKey = k;
            mValue = v;
        }

        @CalledByNative("InnerStructB")
        private long getKey() {
            return mKey;
        }

        @CalledByNative("InnerStructB")
        private String getValue() {
            return mValue;
        }
    }

    List<InnerStructB> mListInnerStructB = new ArrayList<InnerStructB>();

    void iterateAndDoSomethingWithMap() {
        Iterator<InnerStructB> it = mListInnerStructB.iterator();
        while (it.hasNext()) {
            InnerStructB element = it.next();
            // Now, do something with element.
            SampleForTestsJni.get().addStructB(mNativeCPPObject, this, element);
        }
        SampleForTestsJni.get().iterateAndDoSomethingWithStructB(mNativeCPPObject, this);
    }
    interface InnerInterface {}
    enum InnerEnum {}

    @CalledByNative
    static InnerInterface getInnerInterface() {
        return null;
    }

    @CalledByNative
    static InnerEnum getInnerEnum() {
        return null;
    }

    // Test overloads (causes names to be mangled).
    @CalledByNative
    static InnerEnum getInnerEnum(int a) {
        return null;
    }

    // Test jclass and jthrowable, as well as generics.
    @CalledByNative
    private Class<Map<String, String>> getClass(Class<Map<String, String>> arg0) {
        return null;
    }
    @CalledByNative
    private Throwable getThrowable(Throwable arg0) {
        return null;
    }

    @CalledByNative
    static @JniType("std::vector<bool>") boolean[] primitiveArrays(
            @JniType("std::vector<uint8_t>") byte[] b,
            @JniType("std::vector<uint16_t>") char[] c,
            @JniType("std::vector<int16_t>") short[] s,
            @JniType("std::vector<int32_t>") int[] i,
            @JniType("std::vector<int64_t>") long[] l,
            @JniType("std::vector<float>") float[] f,
            @JniType("std::vector<double>") double[] d) {
        return null;
    }

    // ---------------------------------------------------------------------------------------------
    // The following methods demonstrate declaring methods to call into C++ from Java.
    // The generator detects the type and name of the first parameter.
    @NativeMethods
    public interface Natives {
        // This declares a C++ function which the application code must implement:
        // static jint Init(JNIEnv* env, jobject caller);
        // The jobject parameter refers back to this java side object instance.
        // The implementation must return the pointer to the C++ object cast to jint.
        // The caller of this method should store it, and supply it as a the nativeCPPClass param to
        // subsequent native method calls (see the methods below that take an "int native..." as
        // first param).
        long init(
                SampleForTests caller,
                String param,
                @JniType("jni_zero::ByteArrayView") byte[] bytes,
                @JniType("jni_zero::tests::CPPClass*") SampleForTests convertedType,
                @JniType("std::vector") SampleForTests[] nonConvertedArray);

        // This defines a function binding to the associated C++ class member function. The name is
        // derived from |nativeDestroy| and |nativeCPPClass| to arrive at CPPClass::Destroy() (i.e.
        // native prefixes stripped).
        //
        // The |nativeCPPClass| is automatically cast to type CPPClass*, in order to obtain the
        // object on which to invoke the member function. Replace "CPPClass" with your particular
        // class name!
        void destroy(
                long nativeCPPClass,
                SampleForTests caller,
                @JniType("std::vector<uint8_t>") byte[] bytes);

        // This declares a C++ function which the application code must implement:
        // static jdouble GetDoubleFunction(JNIEnv* env, jobject caller);
        // The jobject parameter refers back to this java side object instance.
        double getDoubleFunction(SampleForTests ret);

        // Similar to nativeGetDoubleFunction(), but here the C++ side will receive a jclass rather
        // than jobject param, as the function is declared static.
        float getFloatFunction();

        @JniType("std::vector")
        List<SampleForTests> listTest2(@JniType("std::vector<std::string>") Set<String> items);

        // This function takes a non-POD datatype. We have a list mapping them to their full
        // classpath in jni_generator.py JavaParamToJni. If you require a new datatype, make sure
        // you add to that function.
        void setNonPODDatatype(SampleForTests obj, Rect rect);

        // This declares a C++ function which the application code must implement:
        // static ScopedJavaLocalRef<jobject> GetNonPODDatatype(JNIEnv* env, jobject caller);
        // The jobject parameter refers back to this java side object instance.
        // Note that it returns a ScopedJavaLocalRef<jobject> so that you don' have to worry about
        // deleting the JNI local reference. This is similar with Strings and arrays.
        Object getNonPODDatatype(SampleForTests jcaller);

        // Test jclass and jthrowable, as well as generics.
        Class<Map<String, String>> getClass(Class<Map<String, String>> env);

        Throwable getThrowable(Throwable arg0);

        // Test Map.
        @JniType("std::map<std::string, std::string>")
        Map<String, String> mapTest2(
                @JniType("std::map<std::string, std::string>") Map<String, String> arg0);

        // Test class under the same package
        void classUnderSamePackageTest(SampleUnderSamePackage arg);

        // Similar to nativeDestroy above, this will cast nativeCPPClass into pointer of CPPClass
        // type and call its Method member function. Replace "CPPClass" with your particular class
        // name!
        int method(
                long nativeCPPClass,
                SampleForTests caller,
                @JniType("std::vector<std::string>") String[] strings);

        @JniType("std::vector<bool>")
        boolean[] primitiveArrays(
                @JniType("std::vector<uint8_t>") byte[] b,
                @JniType("std::vector<uint16_t>") char[] c,
                @JniType("std::vector<int16_t>") short[] s,
                @JniType("std::vector<int32_t>") int[] i,
                @JniType("std::vector<int64_t>") long[] l,
                @JniType("std::vector<float>") float[] f,
                @JniType("std::vector<double>") double[] d);

        // Similar to nativeMethod above, but here the C++ fully qualified class name is taken from
        // the annotation rather than parameter name, which can thus be chosen freely.
        @NativeClassQualifiedName("CPPClass::InnerClass")
        double methodOtherP0(long nativePtr, SampleForTests caller);

        // Tests passing a nested class.
        void addStructB(long nativeCPPClass, SampleForTests caller, InnerStructB b);

        void iterateAndDoSomethingWithStructB(long nativeCPPClass, SampleForTests caller);
        String returnAString(long nativeCPPClass, SampleForTests caller);
    }
}
