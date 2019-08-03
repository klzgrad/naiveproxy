// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test;

import android.support.test.internal.runner.listener.InstrumentationRunListener;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.runner.Description;

import org.chromium.base.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.lang.annotation.Annotation;
import java.lang.reflect.Array;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * A RunListener that list out all the test information into a json file.
 */
public class TestListInstrumentationRunListener extends InstrumentationRunListener {
    private static final String TAG = "TestListRunListener";
    private static final Set<String> SKIP_METHODS = new HashSet<>(
            Arrays.asList(new String[] {"toString", "hashCode", "annotationType", "equals"}));

    private final Map<Class<?>, JSONObject> mTestClassJsonMap = new HashMap<>();

    /**
     * Store the test method description to a Map at the beginning of a test run.
     */
    @Override
    public void testStarted(Description desc) throws Exception {
        if (mTestClassJsonMap.containsKey(desc.getTestClass())) {
            ((JSONArray) mTestClassJsonMap.get(desc.getTestClass()).get("methods"))
                .put(getTestMethodJSON(desc));
        } else {
            Class<?> testClass = desc.getTestClass();
            mTestClassJsonMap.put(desc.getTestClass(), new JSONObject()
                    .put("class", testClass.getName())
                    .put("superclass", testClass.getSuperclass().getName())
                    .put("annotations",
                            getAnnotationJSON(Arrays.asList(testClass.getAnnotations())))
                    .put("methods", new JSONArray().put(getTestMethodJSON(desc))));
        }
    }

    /**
     * Create a JSONArray with all the test class JSONObjects and save it to listed output path.
     */
    public void saveTestsToJson(String outputPath) throws IOException {
        Writer writer = null;
        File file = new File(outputPath);
        try {
            writer = new OutputStreamWriter(new FileOutputStream(file), "UTF-8");
            JSONArray allTestClassesJSON = new JSONArray(mTestClassJsonMap.values());
            writer.write(allTestClassesJSON.toString());
        } catch (IOException e) {
            Log.e(TAG, "failed to write json to file", e);
            throw e;
        } finally {
            if (writer != null) {
                try {
                    writer.close();
                } catch (IOException e) {
                    // Intentionally ignore IOException when closing writer
                }
            }
        }
    }

    /**
     * Return a JSONOject that represent a Description of a method".
     */
    static JSONObject getTestMethodJSON(Description desc) throws Exception {
        return new JSONObject()
                .put("method", desc.getMethodName())
                .put("annotations", getAnnotationJSON(desc.getAnnotations()));
    }

    /**
     * Create a JSONObject that represent a collection of anntations.
     *
     * For example, for the following group of annotations for ExampleClass
     * <code>
     * @A
     * @B(message = "hello", level = 3)
     * public class ExampleClass() {}
     * </code>
     *
     * This method would return a JSONObject as such:
     * <code>
     * {
     *   "A": {},
     *   "B": {
     *     "message": "hello",
     *     "level": "3"
     *   }
     * }
     * </code>
     *
     * The method accomplish this by though through each annotation and reflectively call the
     * annotation's method to get the element value, with exceptions to methods like "equals()"
     * or "hashCode".
     */
    static JSONObject getAnnotationJSON(Collection<Annotation> annotations)
            throws Exception {
        JSONObject annotationsJsons = new JSONObject();
        for (Annotation a : annotations) {
            JSONObject elementJsonObject = new JSONObject();
            for (Method method : a.annotationType().getMethods()) {
                if (SKIP_METHODS.contains(method.getName())) {
                    continue;
                }
                try {
                    Object value = method.invoke(a);
                    if (value == null) {
                        elementJsonObject.put(method.getName(), null);
                    } else if (value.getClass().isArray()) {
                        Class<?> componentClass = value.getClass().getComponentType();
                        // Arrays of primitives can't be cast to Object arrays, so we have to
                        // special case them and manually make a copy.
                        // This could be done more cleanly with something like
                        // Arrays.stream(value).boxed().toArray(Integer[]::new), but that requires
                        // a minimum SDK level of 24 to use.
                        Object[] arrayValue = componentClass.isPrimitive()
                                ? copyPrimitiveArrayToObjectArray(value)
                                : ((Object[]) value);
                        elementJsonObject.put(
                                method.getName(), new JSONArray(Arrays.asList(arrayValue)));
                    } else {
                        elementJsonObject.put(method.getName(), value.toString());
                    }
                } catch (IllegalArgumentException e) {
                }
            }
            annotationsJsons.put(a.annotationType().getSimpleName(), elementJsonObject);
        }
        return annotationsJsons;
    }

    private static Object[] copyPrimitiveArrayToObjectArray(Object primitiveArray)
            throws NoSuchMethodException, IllegalAccessException, InvocationTargetException,
                   ClassCastException {
        Class<?> primitiveClass = primitiveArray.getClass();
        Class<?> componentClass = primitiveClass.getComponentType();
        Class<?> wrapperClass = null;
        if (componentClass == Boolean.TYPE) {
            wrapperClass = Boolean.class;
        } else if (componentClass == Byte.TYPE) {
            wrapperClass = Byte.class;
        } else if (componentClass == Character.TYPE) {
            wrapperClass = Character.class;
        } else if (componentClass == Double.TYPE) {
            wrapperClass = Double.class;
        } else if (componentClass == Float.TYPE) {
            wrapperClass = Float.class;
        } else if (componentClass == Integer.TYPE) {
            wrapperClass = Integer.class;
        } else if (componentClass == Long.TYPE) {
            wrapperClass = Long.class;
        } else if (componentClass == Short.TYPE) {
            wrapperClass = Short.class;
        } else {
            // This should only be void since there are 8 primitives + void, but we can't support
            // void.
            throw new ClassCastException(
                    "Cannot cast a primitive void array to Object void array.");
        }
        Method converterMethod = wrapperClass.getMethod("valueOf", componentClass);
        ArrayList<Object> arrayValue = new ArrayList<Object>();
        for (int i = 0; i < Array.getLength(primitiveClass.cast(primitiveArray)); i++) {
            arrayValue.add(
                    converterMethod.invoke(Array.get(primitiveClass.cast(primitiveArray), i)));
        }
        return arrayValue.toArray();
    }
}
