// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;

import java.io.File;
import java.io.IOException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.Collections;

/** Unit tests for {@link Log}. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class FileUtilsTest {
    @Rule
    public final TemporaryFolder temporaryFolder = new TemporaryFolder();

    /**
     * Recursively lists all paths under a directory as relative paths, rendered as a string.
     *
     * @param rootDir The directory {@link Path}.
     * @return A "; "-deliminated string of relative paths of all files stirctly under |rootDir|,
     *         lexicographically by path segments. Directories have "/" as suffix.
     */
    private String listAllPaths(Path rootDir) {
        ArrayList<String> pathList = new ArrayList<String>();
        try {
            Files.walkFileTree(rootDir, new SimpleFileVisitor<Path>() {
                @Override
                public FileVisitResult preVisitDirectory(Path path, BasicFileAttributes attrs)
                        throws IOException {
                    String relPathString = rootDir.relativize(path).toString();
                    if (!relPathString.isEmpty()) { // Exclude |rootDir|.
                        pathList.add(relPathString + "/");
                    }
                    return FileVisitResult.CONTINUE;
                }
                @Override
                public FileVisitResult visitFile(Path path, BasicFileAttributes attrs)
                        throws IOException {
                    pathList.add(rootDir.relativize(path).toString());
                    return FileVisitResult.CONTINUE;
                }
            });
        } catch (IOException e) {
        }

        // Sort paths lexicographically by path segments. For example, "foo.bar/file" and "foo/sub"
        // are treated as ["foo.bar", "file"] and ["foo", "sub"], then compared lexicographically
        // element-by-element. Since "foo.bar" < "foo" (String comparison), so the order is
        // "foo/sub" < "foo.bar/file". Instead of actually splitting the strings into lists, we
        // simply replace '/' with |kSep| as ASCII character 1 for sorting...
        final char kSep = (char) 1;
        for (int i = 0; i < pathList.size(); ++i) {
            pathList.set(i, pathList.get(i).replace('/', kSep));
        }
        Collections.sort(pathList);
        // Then restore '/'.
        for (int i = 0; i < pathList.size(); ++i) {
            pathList.set(i, pathList.get(i).replace(kSep, '/'));
        }
        return String.join("; ", pathList);
    }

    /**
     * Helper to check the current list of temp files and directories matches expectation.
     *
     * @param expectedFileList A string representation of the expected list of temp files and
     *        directories. See listAllPaths() for format.
     */
    private void assertFileList(String expectedFileList) {
        Path rootDir = temporaryFolder.getRoot().toPath();
        assertEquals(expectedFileList, listAllPaths(rootDir));
    }

    /**
     * Helper to get the {@link File} object of a temp file created for testing.
     *
     * @param relPathname The relative name of the temp file or directory.
     */
    private File getFile(String relPathName) {
        Path rootDir = temporaryFolder.getRoot().toPath();
        return rootDir.resolve(relPathName).toFile();
    }

    /**
     * Helper to create a mix of test files and directories. Can be called multiple times per test,
     * but requires the temp file to be empty.
     */
    private void prepareMixedFilesTestCase() throws IOException {
        assertFileList("");
        temporaryFolder.newFolder("a1");
        temporaryFolder.newFolder("a1", "b1");
        temporaryFolder.newFile("a1/b1/c");
        temporaryFolder.newFile("a1/b1/c2");
        temporaryFolder.newFolder("a1", "b2");
        temporaryFolder.newFolder("a1", "b2", "c");
        temporaryFolder.newFile("a1/b3");
        temporaryFolder.newFolder("a2");
        temporaryFolder.newFile("c");
    }

    @Test
    public void testRecursivelyDeleteFileBasic() throws IOException {
        // Test file deletion.
        temporaryFolder.newFile("some_File");
        temporaryFolder.newFile("some");
        temporaryFolder.newFile(".dot-config1");
        temporaryFolder.newFile("some_File.txt");
        assertFileList(".dot-config1; some; some_File; some_File.txt");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("some_File"), null));
        assertFileList(".dot-config1; some; some_File.txt");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("some"), null));
        assertFileList(".dot-config1; some_File.txt");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("ok_to_delete_nonexistent"), null));
        assertFileList(".dot-config1; some_File.txt");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile(".dot-config1"), null));
        assertFileList("some_File.txt");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("some_File.txt"), null));
        assertFileList("");

        // Test directory deletion.
        temporaryFolder.newFolder("some_Dir");
        temporaryFolder.newFolder("some");
        temporaryFolder.newFolder(".dot-dir2");
        temporaryFolder.newFolder("some_Dir.ext");
        assertFileList(".dot-dir2/; some/; some_Dir/; some_Dir.ext/");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("some_Dir"), null));
        assertFileList(".dot-dir2/; some/; some_Dir.ext/");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("some"), null));
        assertFileList(".dot-dir2/; some_Dir.ext/");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("ok/to/delete/nonexistent"), null));
        assertFileList(".dot-dir2/; some_Dir.ext/");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile(".dot-dir2"), null));
        assertFileList("some_Dir.ext/");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("some_Dir.ext"), null));
        assertFileList("");

        // Test recursive deletion of mixed files and directories.
        for (int i = 0; i < 2; ++i) {
            Function<String, Boolean> canDelete = (i == 0) ? null : FileUtils.DELETE_ALL;
            prepareMixedFilesTestCase();
            assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b2/; a1/b2/c/; a1/b3; a2/; c");
            assertTrue(FileUtils.recursivelyDeleteFile(getFile("c"), canDelete));
            assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b2/; a1/b2/c/; a1/b3; a2/");
            assertTrue(FileUtils.recursivelyDeleteFile(getFile("a1/b1"), canDelete));
            assertFileList("a1/; a1/b2/; a1/b2/c/; a1/b3; a2/");
            assertTrue(FileUtils.recursivelyDeleteFile(getFile("a1"), canDelete));
            assertFileList("a2/");
            assertTrue(FileUtils.recursivelyDeleteFile(getFile("a2"), canDelete));
            assertFileList("");
        }
    }

    // Enable or delete once https://crbug.com/1066733 is fixed.
    @Ignore
    @Test
    public void testRecursivelyDeleteFileWithCanDelete() throws IOException {
        Function<String, Boolean> canDeleteIfEndsWith1 = (String filepath) -> {
            return filepath.endsWith("1");
        };
        Function<String, Boolean> canDeleteIfEndsWith2 = (String filepath) -> {
            return filepath.endsWith("2");
        };

        prepareMixedFilesTestCase();
        assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b2/; a1/b2/c/; a1/b3; a2/; c");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("a1"), canDeleteIfEndsWith1));
        assertFileList("a2/; c");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("a2"), canDeleteIfEndsWith1));
        assertFileList("a2/; c");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("a2"), canDeleteIfEndsWith2));
        assertFileList("c");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("a1"), null));
        assertFileList("");

        prepareMixedFilesTestCase();
        assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b2/; a1/b2/c/; a1/b3; a2/; c");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("a1"), canDeleteIfEndsWith2));
        assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b3; a2/; c");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("c"), canDeleteIfEndsWith2));
        assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b3; a2/; c");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("c"), null));
        assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b3; a2/");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("a2"), canDeleteIfEndsWith2));
        assertFileList("a1/; a1/b1/; a1/b1/c; a1/b1/c2; a1/b3");
        assertTrue(FileUtils.recursivelyDeleteFile(getFile("a1"), null));
        assertFileList("");
    }

    // TOOD(huangs): Implement testBatchDeleteFiles().
    // TOOD(huangs): Implement testExtractAsset().
    // TOOD(huangs): Implement testCopyStream().
    // TOOD(huangs): Implement testCopyStreamToFile().
    // TOOD(huangs): Implement testReadStream().
    // TOOD(huangs): Implement testGetUriForFile().

    @Test
    public void testGetExtension() {
        assertEquals("txt", FileUtils.getExtension("foo.txt"));
        assertEquals("txt", FileUtils.getExtension("fOo.TxT"));
        assertEquals("", FileUtils.getExtension(""));
        assertEquals("", FileUtils.getExtension("No_extension"));
        assertEquals("foo_config", FileUtils.getExtension(".foo_conFIG"));
        assertEquals("6", FileUtils.getExtension("a.1.2.3.4.5.6"));
        assertEquals("a1z2_a8z9", FileUtils.getExtension("a....a1z2_A8Z9"));
        assertEquals("", FileUtils.getExtension("dotAtEnd."));
        assertEquals("ext", FileUtils.getExtension("/Full/PATH/To/File.Ext"));
        assertEquals("", FileUtils.getExtension("/Full.PATH/To.File/Extra"));
        assertEquals("", FileUtils.getExtension("../../file"));
        assertEquals("", FileUtils.getExtension("./etc/passwd"));
        assertEquals("", FileUtils.getExtension("////////"));
        assertEquals("", FileUtils.getExtension("........"));
        assertEquals("", FileUtils.getExtension("././././"));
        assertEquals("", FileUtils.getExtension("/./././."));
    }

    // TOOD(huangs): Implement testQueryBitmapFromContentProvider().
}
