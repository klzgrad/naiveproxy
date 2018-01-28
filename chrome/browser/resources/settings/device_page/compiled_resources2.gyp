# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'device_page',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        'device_page_browser_proxy'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'device_page_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'pointers',
      'dependencies': [
        '../controls/compiled_resources2.gyp:settings_toggle_button',
        'device_page_browser_proxy'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'keyboard',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '../prefs/compiled_resources2.gyp:prefs_types',
        '../controls/compiled_resources2.gyp:settings_dropdown_menu',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):settings_private',
        'device_page_browser_proxy'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'stylus',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_indicator',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '../prefs/compiled_resources2.gyp:prefs_types',
        'device_page_browser_proxy'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'display',
      'dependencies': [
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_without_ink',
        '<(EXTERNS_GYP):settings_private',
        '<(EXTERNS_GYP):system_display',
        '<(INTERFACES_GYP):system_display_interface',
        'display_layout'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'display_layout',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-resizable-behavior/compiled_resources2.gyp:iron-resizable-behavior-extracted',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):system_display',
        '<(INTERFACES_GYP):system_display_interface',
        'drag_behavior',
        'layout_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'display_overscan_dialog',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):system_display',
        '<(INTERFACES_GYP):system_display_interface',
        'display'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'drag_behavior',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'layout_behavior',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):system_display',
        '<(INTERFACES_GYP):system_display_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'night_light_slider',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-a11y-keys-behavior/compiled_resources2.gyp:iron-a11y-keys-behavior-extracted',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-resizable-behavior/compiled_resources2.gyp:iron-resizable-behavior-extracted',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-behaviors/compiled_resources2.gyp:paper-inky-focus-behavior-extracted',
        '../prefs/compiled_resources2.gyp:prefs_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'power',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        '<(EXTERNS_GYP):settings_private',
        '../compiled_resources2.gyp:route',
        '../prefs/compiled_resources2.gyp:prefs_types',
        'device_page_browser_proxy'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'storage',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_without_ink',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'drive_cache_dialog',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):chrome_send',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
 ],
}
