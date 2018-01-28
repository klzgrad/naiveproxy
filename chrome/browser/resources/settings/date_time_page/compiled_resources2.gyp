# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'date_time_page',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '../prefs/compiled_resources2.gyp:prefs_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_indicator_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        'date_time_types',
        'timezone_selector',
        'timezone_subpage',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'date_time_types',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'timezone_selector',
      'dependencies': [
        '../controls/compiled_resources2.gyp:settings_dropdown_menu',
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '../prefs/compiled_resources2.gyp:prefs_types',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        'date_time_types',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'timezone_subpage',
      'dependencies': [
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        'date_time_types',
        'timezone_selector',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
