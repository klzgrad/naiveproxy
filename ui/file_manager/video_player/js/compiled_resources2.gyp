# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'background',
      'dependencies': [
        '../../file_manager/background/js/compiled_resources2.gyp:app_window_wrapper',
        '../../file_manager/background/js/compiled_resources2.gyp:background_base',
        'error_util',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'error_util',
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'media_controls',
      'dependencies': [
        '../../file_manager/common/js/compiled_resources2.gyp:util',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-slider/compiled_resources2.gyp:paper-slider-extracted',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:menu_button',
        '<(EXTERNS_GYP):media_player_private',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'video_player',
      'dependencies': [
        '../../externs/compiled_resources2.gyp:chrome_cast',
        '../../file_manager/common/js/compiled_resources2.gyp:metrics',
        '../../file_manager/common/js/compiled_resources2.gyp:util',
        '../../file_manager/foreground/js/compiled_resources2.gyp:mouse_inactivity_watcher',
        '../../file_manager/foreground/js/compiled_resources2.gyp:volume_manager_wrapper',
        '../../image_loader/compiled_resources2.gyp:image_loader_client',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_template_no_process',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:menu',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:menu_item',
        'cast/compiled_resources2.gyp:cast_video_element',
        'cast/compiled_resources2.gyp:media_manager',
        'error_util',
        'media_controls',
        'video_player_metrics',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'video_player_metrics',
      'dependencies': [
        '../../file_manager/common/js/compiled_resources2.gyp:metrics_base',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
  ],
}
