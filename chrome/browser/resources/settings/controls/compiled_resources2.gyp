# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'controlled_button',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_pref_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'pref_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'controlled_radio_button',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-a11y-keys-behavior/compiled_resources2.gyp:iron-a11y-keys-behavior-extracted',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(EXTERNS_GYP):settings_private',
        '../prefs/compiled_resources2.gyp:pref_util',
        'pref_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'extension_controlled_indicator',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '../compiled_resources2.gyp:extension_control_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'important_site_checkbox',
      'dependencies': [
        '../clear_browsing_data_dialog/compiled_resources2.gyp:clear_browsing_data_browser_proxy',
        'settings_boolean_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'pref_control_behavior',
      'dependencies': [
        '../prefs/compiled_resources2.gyp:prefs_types',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_boolean_control_behavior',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_pref_behavior',
        '../prefs/compiled_resources2.gyp:prefs_types',
        'pref_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_checkbox',
      'dependencies': [
        'settings_boolean_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_dropdown_menu',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_indicator_behavior',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_pref_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):settings_private',
        '../prefs/compiled_resources2.gyp:pref_util',
        'pref_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_radio_group',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):settings_private',
        '../prefs/compiled_resources2.gyp:pref_util',
        'pref_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_slider',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-slider/compiled_resources2.gyp:paper-slider-extracted',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_pref_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_toggle_button',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_toggle/compiled_resources2.gyp:cr_toggle',
        'settings_boolean_control_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
