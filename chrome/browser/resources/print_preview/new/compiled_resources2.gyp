# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'app',
      'dependencies': [
        'header',
        'destination_settings',
        'pages_settings',
        'copies_settings',
        'layout_settings',
        'color_settings',
        'media_size_settings',
        'margins_settings',
        'dpi_settings',
        'scaling_settings',
        'other_options_settings',
        'advanced_options_settings',
        'model',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'header',
      'dependencies': [
        '../data/compiled_resources2.gyp:destination',
        'settings_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'destination_settings',
      'dependencies': [
        '../data/compiled_resources2.gyp:destination',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'pages_settings',
      'dependencies': [
        'settings_behavior',
        '../data/compiled_resources2.gyp:document_info',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'copies_settings',
      'dependencies': [
        'number_settings_section',
        'settings_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'layout_settings',
      'dependencies': [
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'color_settings',
      'dependencies': [
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'media_size_settings',
      'dependencies': [
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'margins_settings',
      'dependencies': [
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'dpi_settings',
      'dependencies': [
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'scaling_settings',
      'dependencies': [
        '../data/compiled_resources2.gyp:document_info',
        'number_settings_section',
        'settings_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'other_options_settings',
      'dependencies': [
        'settings_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'advanced_options_settings',
      'dependencies': [
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'number_settings_section',
      'dependencies': [
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_behavior',
      'dependencies': [
        'model',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'model',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '../data/compiled_resources2.gyp:destination',
        '../data/compiled_resources2.gyp:document_info',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    }
  ],
}
