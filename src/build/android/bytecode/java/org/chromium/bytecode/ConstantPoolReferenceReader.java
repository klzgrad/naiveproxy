// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.bytecode;

import org.objectweb.asm.ClassReader;

import java.io.BufferedInputStream;
import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Compiles list of references from all Java .class files in given jar paths by
 * reading from the constant pool, and writes this list to an output file.
 * This list is used for keep rule generation for maintaining compatibility between
 * async DFMs and synchronous modules.
 */
public class ConstantPoolReferenceReader {
    private static final String CLASS_FILE_SUFFIX = ".class";
    private static final int BUFFER_SIZE = 16384;

    // Constants representing Java constant pool tags
    // See https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html#jvms-4.4
    private static final int FIELD_REF_TAG = 9;
    private static final int METHOD_REF_TAG = 10;
    private static final int INTERFACE_METHOD_REF_TAG = 11;

    private static byte[] readAllBytes(InputStream inputStream) throws IOException {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int numRead = 0;
        byte[] data = new byte[BUFFER_SIZE];
        while ((numRead = inputStream.read(data, 0, data.length)) != -1) {
            buffer.write(data, 0, numRead);
        }
        return buffer.toByteArray();
    }

    /**
     * Given a set of paths, generates references used to produce Proguard keep rules
     * necessary for asynchronous DFMs.
     * It reads all references stored in constant pools of Java classes from
     * the specified jar paths and writes them to an output file.
     * References written to the specified file can be converted to a
     * corresponding set of Proguard keep rules using the
     * constant_pool_refs_to_keep_rules.py script.
     *
     * @param jarPaths Set of paths specifying Java files to read constant pool
     * references from.
     * @param outputFilePath File path to write output to.
     */
    public static void writeConstantPoolRefsToFile(Set<String> jarPaths, String outputFilePath) {
        HashSet<String> classReferences = new HashSet<>();

        for (String jarPath : jarPaths) {
            try (ZipInputStream inputStream = new ZipInputStream(
                         new BufferedInputStream(new FileInputStream(jarPath)))) {
                ZipEntry entry;
                while ((entry = inputStream.getNextEntry()) != null) {
                    if (entry.isDirectory() || !entry.getName().endsWith(CLASS_FILE_SUFFIX)) {
                        continue;
                    }
                    byte[] data = readAllBytes(inputStream);
                    ClassReader reader = new ClassReader(data);
                    classReferences.addAll(collectConstantPoolClassReferences(reader));
                }
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        }

        try {
            BufferedWriter writer = new BufferedWriter(new FileWriter(outputFilePath));
            for (String ref : classReferences) {
                writer.append(ref);
                writer.append("\n");
            }
            writer.close();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    /**
     * Given a ClassReader, return a set of all super classes, implemented interfaces and
     * members by reading from the associated class's constant pool.
     *
     * @param classReader .class file interface for reading the constant pool.
     */
    private static Set<String> collectConstantPoolClassReferences(ClassReader classReader) {
        char[] charBuffer = new char[classReader.getMaxStringLength()];
        HashSet<String> classReferences = new HashSet<>();

        classReferences.add(classReader.getSuperName());
        classReferences.addAll(Arrays.asList(classReader.getInterfaces()));

        // According to the Java spec, the constant pool is indexed from 1 to constant_pool_count -
        // 1. See https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html#jvms-4.4
        StringBuilder refInfoString = new StringBuilder();
        for (int i = 1; i < classReader.getItemCount(); i++) {
            int offset = classReader.getItem(i);
            if (offset <= 0) {
                continue;
            }
            int constantType = classReader.readByte(offset - 1);
            if (offset > 0
                    && (constantType == METHOD_REF_TAG || constantType == FIELD_REF_TAG
                            || constantType == INTERFACE_METHOD_REF_TAG)) {
                // Read the corresponding class ref and member info from the constant pool.
                int classIndex = classReader.readUnsignedShort(offset);
                int classStartIndex = classReader.getItem(classIndex);
                // Class index is a 2-byte quantity, nameAndTypeIndex is stored sequentially after.
                int nameAndTypeIndex = classReader.readUnsignedShort(offset + 2);
                int nameAndTypeStartIndex = classReader.getItem(nameAndTypeIndex);

                // Get member's containing class's name, member's name, and member's details (type,
                // return type, and argument types).
                refInfoString.append(classReader.readUTF8(classStartIndex, charBuffer));
                refInfoString.append(",");
                refInfoString.append(classReader.readUTF8(nameAndTypeStartIndex, charBuffer));
                refInfoString.append(",");
                refInfoString.append(classReader.readUTF8(nameAndTypeStartIndex + 2, charBuffer));

                classReferences.add(refInfoString.toString());
                refInfoString.setLength(0);
            }
        }

        return classReferences;
    }
}