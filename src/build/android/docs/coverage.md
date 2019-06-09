# Android code coverage instructions

These are instructions for collecting code coverage data for android
instrumentation and junit tests.

[TOC]

## How Jacoco coverage works

In order to use Jacoco code coverage, we need to create build time pre-instrumented
class files and runtime **.exec** files. Then we need to process them using the
**build/android/generate_jacoco_report.py** script.

## How to collect coverage data

1. Use the following GN build arguments:

  ```gn
  target_os = "android"
  jacoco_coverage = true
  ```

   Now when building, pre-instrumented files will be created in the build directory.

2. Run tests, with option `--coverage-dir <directory>`, to specify where to save
   the .exec file. For example, you can run chrome junit tests:
   `out/Debug/bin/run_chrome_junit_tests --coverage-dir /tmp/coverage`.

3. The coverage results of junit and instrumentation tests will be merged
   automatically if they are in the same directory.

## How to generate coverage report

1. Now we have generated .exec files already. We can create a Jacoco html/xml/csv
   report using `generate_jacoco_report.py`, for example:

  ```shell
  build/android/generate_jacoco_report.py \
     --format html \
     --output-dir tmp/coverage_report/ \
     --coverage-dir /tmp/coverage/ \
     --sources-json-dir out/Debug/ \
  ```
   Then an index.html containing coverage info will be created in output directory:

  ```
  [INFO] Loading execution data file /tmp/coverage/testTitle.exec.
  [INFO] Loading execution data file /tmp/coverage/testSelected.exec.
  [INFO] Loading execution data file /tmp/coverage/testClickToSelect.exec.
  [INFO] Loading execution data file /tmp/coverage/testClickToClose.exec.
  [INFO] Loading execution data file /tmp/coverage/testThumbnail.exec.
  [INFO] Analyzing 58 classes.
  ```

   For xml and csv reports, we need to specify `--output-file` instead of `--output-dir` since
   only one file will be generated as xml or csv report.
  ```shell
  build/android/generate_jacoco_report.py \
    --format xml \
    --output-file tmp/coverage_report/report.xml \
    --coverage-dir /tmp/coverage/ \
    --sources-json-dir out/Debug/ \
  ```

   or

  ```shell
  build/android/generate_jacoco_report.py \
    --format csv \
    --output-file tmp/coverage_report/report.csv \
    --coverage-dir /tmp/coverage/ \
    --sources-json-dir out/Debug/ \
  ```

2. We can also generate JSON format report for
   [coverage tool](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/code_coverage.md)
   created by Chrome Ops - CATS team.
   In this case, we need to change `--format` to json, for example:

  ```shell
  build/android/generate_jacoco_report.py \
    --format json \
    --output-file tmp/coverage_report/coverage.json \
    --coverage-dir /tmp/coverage/ \
    --sources-json-dir out/Debug/ \
  ```

