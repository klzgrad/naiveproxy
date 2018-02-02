# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'app_window_wrapper',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:async_util',
        '../../common/js/compiled_resources2.gyp:util',
        '<(EXTERNS_GYP):chrome_extensions',
        'app_windows',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'app_windows',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
        '../../../externs/compiled_resources2.gyp:app_window_common'
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'background',
      'dependencies': [
        '../../../externs/background/compiled_resources2.gyp:file_browser_background_full',
        '../../../externs/compiled_resources2.gyp:volume_manager',
        '../../common/js/compiled_resources2.gyp:metrics',
        '../../common/js/compiled_resources2.gyp:util',
        '../../common/js/compiled_resources2.gyp:volume_manager_common',
        '<(EXTERNS_GYP):chrome_extensions',
        'app_windows',
        'background_base',
        'device_handler',
        'drive_sync_handler',
        'duplicate_finder',
        'file_operation_handler',
        'file_operation_manager',
        'import_history',
        'launcher',
        'launcher_search',
        'media_import_handler',
        'progress_center',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'background_base',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):file_manager_private',
        'app_windows',
        'volume_manager_factory',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'device_handler',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:async_util',
        '../../common/js/compiled_resources2.gyp:importer_common',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):file_manager_private',
        'volume_manager_factory',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'drive_sync_handler',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:connection',
        'progress_center',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'duplicate_finder',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:volume_manager',
        '../../common/js/compiled_resources2.gyp:importer_common',
        '../../common/js/compiled_resources2.gyp:lru_cache',
        '../../common/js/compiled_resources2.gyp:metrics',
        '<(DEPTH)/third_party/analytics/compiled_resources2.gyp:externs',
        '<(EXTERNS_GYP):file_manager_private',
        'import_history',
        'volume_manager_factory',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'entry_location_impl',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:entry_location',
        '../../common/js/compiled_resources2.gyp:volume_manager_common',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'file_operation_handler',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:progress_center_common',
        'file_operation_manager',
        'progress_center',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'file_operation_manager',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:volume_manager',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        'file_operation_util',
        'volume_manager_factory',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'file_operation_util',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:file_operation_progress_event',
        '../../common/js/compiled_resources2.gyp:async_util',
        '../../common/js/compiled_resources2.gyp:util',
        'metadata_proxy',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_proxy',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:lru_cache',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'import_history',
      'dependencies': [
        '../../../externs/background/compiled_resources2.gyp:import_history',
        '../../common/js/compiled_resources2.gyp:importer_common',
        '../../common/js/compiled_resources2.gyp:metrics',
        '../../common/js/compiled_resources2.gyp:metrics_events',
        '../../common/js/compiled_resources2.gyp:util',
        'metadata_proxy',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'launcher',
      'dependencies': [
        'app_window_wrapper',
        'app_windows',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'launcher_search',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:launcher_search_provider',
        '../../common/js/compiled_resources2.gyp:file_type',
        '../../common/js/compiled_resources2.gyp:util',
        '<(EXTERNS_GYP):file_manager_private',
        'launcher',
        'volume_manager_factory',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'media_import_handler',
      'dependencies': [
        '../../../externs/background/compiled_resources2.gyp:import_runner',
        '../../common/js/compiled_resources2.gyp:importer_common',
        '../../common/js/compiled_resources2.gyp:metrics',
        'import_history',
        'media_scanner',
        'progress_center',
        'task_queue',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'media_scanner',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:platform',
        '../../../externs/background/compiled_resources2.gyp:media_scanner',
        '../../common/js/compiled_resources2.gyp:importer_common',
        '<(EXTERNS_GYP):file_manager_private',
        'file_operation_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'mock_background',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'mock_file_operation_manager',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'mock_media_scanner',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'mock_progress_center',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'mock_volume_manager',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'progress_center',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:async_util',
        '../../common/js/compiled_resources2.gyp:progress_center_common',
        '../../common/js/compiled_resources2.gyp:util',
        '../../foreground/js/ui/compiled_resources2.gyp:progress_center_panel',
        '<(EXTERNS_GYP):chrome_extensions',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'task_queue',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:importer_common',
        'duplicate_finder',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'test_duplicate_finder',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'test_import_history',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'test_util',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'test_util_base',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'volume_info_impl',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:platform',
        '../../../externs/compiled_resources2.gyp:volume_info',
        '../../common/js/compiled_resources2.gyp:volume_manager_common',
        '<(EXTERNS_GYP):command_line_private',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'volume_info_list_impl',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:volume_info',
        '../../../externs/compiled_resources2.gyp:volume_info_list',
        '../../common/js/compiled_resources2.gyp:util',
        './compiled_resources2.gyp:volume_info_impl',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:ui',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:array_data_model',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'volume_manager_impl',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:volume_manager',
        '../../common/js/compiled_resources2.gyp:async_util',
        'entry_location_impl',
        'volume_info_list_impl',
        'volume_manager_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'volume_manager_factory',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:volume_manager',
        'volume_manager_impl',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'volume_manager_util',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:metrics',
        '../../common/js/compiled_resources2.gyp:metrics_events',
        '../../common/js/compiled_resources2.gyp:util',
        '../../common/js/compiled_resources2.gyp:volume_manager_common',
        'volume_info_impl',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
  ],
}
