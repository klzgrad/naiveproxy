# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'audio_player',
      'dependencies': [
        'control_panel',
        'track_info_panel',
        'track_list',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'control_panel',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-slider/compiled_resources2.gyp:paper-slider-extracted',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'repeat_button',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'repeat_button',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-behaviors/compiled_resources2.gyp:iron-button-state-extracted',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-behaviors/compiled_resources2.gyp:iron-control-state-extracted',
      ],
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'track_info_panel',
      'includes': ['../../compile_js2.gypi'],
    },
    {
      'target_name': 'track_list',
      'includes': ['../../compile_js2.gypi'],
    },
  ],
}
