# Building Dex

This doc aims to describe the Chrome build process that takes a set of `.java`
files and turns them into a `classes.dex` file.

[TOC]

## Core GN Target Types

The following have `supports_android` and `requires_android` set to false by
default:
* `java_library()`: Compiles `.java` -> `.jar`
* `java_prebuilt()`:  Imports a prebuilt `.jar` file.

The following have `supports_android` and `requires_android` set to true. They
also have a default `jar_excluded_patterns` set (more on that later):
* `android_library()`
* `android_java_prebuilt()`

All targets names must end with "_java" so that the build system can distinguish
them from non-java targets (or [other variations](https://cs.chromium.org/chromium/src/build/config/android/internal_rules.gni?rcl=ec2c17d7b4e424e060c3c7972842af87343526a1&l=20)).

## Step 1a: Compile with javac

This step is the only step that does not apply to prebuilt targets.

* All `.java` files in a target are compiled by `javac` into `.class` files.
  * This includes `.java` files that live within `.srcjar` files, referenced
    through `srcjar_deps`.
* The `classpath` used when compiling a target is comprised of `.jar` files of
  its deps.
  * When deps are library targets, the Step 1 `.jar` file is used.
  * When deps are prebuilt targets, the original `.jar` file is used.
  * All `.jar` processing done in subsequent steps does not impact compilation
    classpath.
* `.class` files are zipped into an output `.jar` file.
* There is **no support** for incremental compilation at this level.
  * If one source file changes within a library, then the entire library is
    recompiled.
  * Prefer smaller targets to avoid slow compiles.

## Step 1b: Compile with ErrorProne

This step can be disabled via GN arg: `use_errorprone_java_compiler = false`

* Concurrently with step 1a: [ErrorProne] compiles java files and checks for bug
  patterns, including some [custom to Chromium][ep_plugins].
* ErrorProne used to replace step 1a, but was changed to a concurrent step after
  being identified as being slower.

[ErrorProne]: https://errorprone.info/
[ep_plugins]: /tools/android/errorprone_plugin/

## Step 2: Creating an .interface.jar

This step happens in parallel with subsequent steps.

* `//third_party/ijar` converts the `.jar` into an `.interface.jar`, which is a
  copy of the input with all non-public symbols and function bodies removed.
* Dependant targets use `.interface.jar` files to skip having to be rebuilt
  when only private implementation details change.

## Step 3: Bytecode Processing

* `//build/android/bytecode` runs on the compiled `.jar` in order to:
  * Enable Java assertions (when dcheck is enabled).
  * Assert that libraries have properly declared `deps`.

## Step 4: Desugaring

This step happens only when targets have `supports_android = true`.

* `//third_party/bazel/desugar` converts certain Java 8 constructs, such as
  lambdas and default interface methods, into constructs that are compatible
  with Java 7.

## Step 5: Filtering

This step happens only when targets that have `jar_excluded_patterns` or
`jar_included_patterns` set (e.g. all `android_` targets).

* Remove `.class` files that match the filters from the `.jar`. These `.class`
  files are generally those that are re-created with different implementations
  further on in the build process.
  * E.g.: `R.class` files - a part of [Android Resources].
  * E.g.: `GEN_JNI.class` - a part of our [JNI] glue.
  * E.g.: `AppHooksImpl.class` - how `chrome_java` wires up different
    implementations for [non-public builds][apphooks].

[JNI]: /base/android/jni_generator/README.md
[Android Resources]: life_of_a_resource.md
[apphooks]: /chrome/android/java/src/org/chromium/chrome/browser/AppHooksImpl.java

## Step 6: Instrumentation

This step happens only when this GN arg is set: `use_jacoco_coverage = true`

* [Jacoco] adds instrumentation hooks to methods.

[Jacoco]: https://www.eclemma.org/jacoco/

## Step 7: Copy to lib.java

* The `.jar` is copied into `$root_build_dir/lib.java` (under target-specific
  subdirectories) so that it will be included by bot archive steps.
  * These `.jar` files are the ones used when running `java_binary` and
    `junit_binary` targets.

## Step 8: Per-Library Dexing

This step happens only when targets have `supports_android = true`.

* [d8] converts `.jar` files contain `.class` files into `.dex.jar` files
  containing `.dex` files.
* Dexing is incremental - it will reuse dex'ed classes from a previous build if
  the corresponding `.class` file is unchanged.
* These per-library `.dex.jar` files are used directly by [incremental install],
  and are inputs to the Apk step when `enable_proguard = false`.
  * Even when `is_java_debug = false`, many apk targets do not enable ProGuard
    (e.g. unit tests).

[d8]: https://developer.android.com/studio/command-line/d8
[incremental install]: /build/android/incremental_install/README.md

## Step 9: Apk / Bundle Module Compile

* Each `android_apk` and `android_bundle_module` template has a nested
  `java_library` target. The nested library includes final copies of files
  stripped out by prior filtering steps. These files include:
  * Final `R.java` files, created by `compile_resources.py`.
  * Final `GEN_JNI.java` for JNI glue.
  * `BuildConfig.java` and `NativeLibraries.java` (//base dependencies).

## Step 10: Final Dexing

When `is_java_debug = true`:
* [d8] merges all library `.dex.jar` files into a final `.dex.zip`.

When `is_java_debug = false`:
* [R8] performs whole-program optimization on all library `lib.java` `.jar`
  files and outputs a final `.dex.zip`.
  * For App Bundles, R8 creates a single `.dex.zip` with the code from all
    modules.

[R8]: https://r8.googlesource.com/r8

## Step 11: Bundle Module Dex Splitting

This step happens only when `is_java_debug = false`.

* [dexsplitter.py] splits the single `.dex.zip` into per-module `.dex.zip`
  files.
