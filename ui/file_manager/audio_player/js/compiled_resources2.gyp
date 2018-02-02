# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'audio_player',
      'dependencies': [
        '../../externs/compiled_resources2.gyp:audio_player_foreground',
        '../../file_manager/common/js/compiled_resources2.gyp:util',
        '../../file_manager/foreground/js/compiled_resources2.gyp:volume_manager_wrapper',
        '../../file_manager/foreground/js/metadata/compiled_resources2.gyp:content_metadata_provider',
        '../../file_manager/foreground/js/metadata/compiled_resources2.gyp:metadata_model',
        '../elements/compiled_resources2.gyp:audio_player',
        '../elements/compiled_resources2.gyp:track_list',
        '<(EXTERNS_GYP):file_manager_private',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'background',
      'dependencies': [
        '../../file_manager/background/js/compiled_resources2.gyp:app_window_wrapper',
        '../../file_manager/background/js/compiled_resources2.gyp:background_base',
        '../../file_manager/common/js/compiled_resources2.gyp:file_type',
        '../../file_manager/common/js/compiled_resources2.gyp:util',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'error_util',
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_worker',
      'includes': ['../../compile_js2.gypi'],
    },
  ],
}
