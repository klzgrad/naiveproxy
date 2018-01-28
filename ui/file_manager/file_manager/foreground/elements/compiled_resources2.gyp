# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'files_icon_button',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-behaviors/compiled_resources2.gyp:iron-button-state-extracted',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-behaviors/compiled_resources2.gyp:iron-control-state-extracted',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_metadata_box',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_metadata_entry',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_quick_view',
      'dependencies': [
        'files_metadata_box',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_ripple',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_safe_media',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_safe_media_webview_content',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_toast',
      'dependencies': [
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_toggle_ripple',
      'dependencies': [
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'files_tooltip',
      'includes': ['../../../compile_js2.gypi'],
    },
  ],
}
