# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'async_util',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'error_util',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'file_type',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'importer_common',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:background_window',
        '../../../externs/background/compiled_resources2.gyp:file_browser_background',
        '<(DEPTH)/third_party/analytics/compiled_resources2.gyp:externs',
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):command_line_private',
        'file_type',
        'volume_manager_common',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'lru_cache',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metrics',
      'dependencies': [
        '<(DEPTH)/third_party/analytics/compiled_resources2.gyp:externs',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(EXTERNS_GYP):file_manager_private',
        '<(EXTERNS_GYP):chrome_extensions',
        'metrics_base',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metrics_base',
      'dependencies': [
        '<(EXTERNS_GYP):metrics_private',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metrics_events',
      'dependencies': [
        '<(DEPTH)/third_party/analytics/compiled_resources2.gyp:externs',
        'metrics_base',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'progress_center_common',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'util',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:app_window_common',
        '../../../externs/compiled_resources2.gyp:entry_location',
        '../../../externs/compiled_resources2.gyp:platform',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:ui',
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):command_line_private',
        '<(EXTERNS_GYP):file_manager_private',
        'volume_manager_common',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'volume_manager_common',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:volume_info',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
  ],
}
