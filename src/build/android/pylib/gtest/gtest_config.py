# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Configuration file for android gtest suites."""

# Add new suites here before upgrading them to the stable list below.
EXPERIMENTAL_TEST_SUITES = [
    'components_browsertests',
    'heap_profiler_unittests',
    'devtools_bridge_tests',
]

TELEMETRY_EXPERIMENTAL_TEST_SUITES = [
    'telemetry_unittests',
]

# Do not modify this list without approval of an android owner.
# This list determines which suites are run by default, both for local
# testing and on android trybots running on commit-queue.
STABLE_TEST_SUITES = [
    'android_webview_unittests',
    'base_unittests',
    'blink_unittests',
    'cc_unittests',
    'components_unittests',
    'content_browsertests',
    'content_unittests',
    'events_unittests',
    'gl_tests',
    'gl_unittests',
    'gpu_unittests',
    'ipc_tests',
    'media_unittests',
    'midi_unittests',
    'net_unittests',
    'sandbox_linux_unittests',
    'skia_unittests',
    'sql_unittests',
    'storage_unittests',
    'ui_android_unittests',
    'ui_base_unittests',
    'ui_touch_selection_unittests',
    'unit_tests_apk',
]

# Tests fail in component=shared_library build, which is required for ASan.
# http://crbug.com/344868
ASAN_EXCLUDED_TEST_SUITES = [
    'sandbox_linux_unittests',

    # The internal ASAN recipe cannot run step "unit_tests_apk", this is the
    # only internal recipe affected. See http://crbug.com/607850
    'unit_tests_apk',
]
