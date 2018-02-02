# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'cast_extension_discoverer',
      'dependencies': [
        '../compiled_resources2.gyp:error_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'cast_video_element',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:chrome_cast',
        '../../../externs/compiled_resources2.gyp:platform',
        '../compiled_resources2.gyp:error_util',
        '../compiled_resources2.gyp:video_player_metrics',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        'media_manager',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'caster',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:chrome_cast',
        '../compiled_resources2.gyp:video_player',
        'cast_extension_discoverer',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'media_manager',
      'dependencies': [
        '<(EXTERNS_GYP):file_manager_private',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
  ],
}
