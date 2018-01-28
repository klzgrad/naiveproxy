# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'appearance_fonts_page',
      'dependencies': [
        '../controls/compiled_resources2.gyp:settings_dropdown_menu',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        '<(EXTERNS_GYP):chrome_send',
        'fonts_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'appearance_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):chrome_send',
        '<(EXTERNS_GYP):management',
        '<(EXTERNS_GYP):settings_private',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'appearance_page',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):settings_private',
        '<(EXTERNS_GYP):chrome_send',
        '../compiled_resources2.gyp:route',
        '../controls/compiled_resources2.gyp:settings_dropdown_menu',
        '../settings_page/compiled_resources2.gyp:settings_animated_pages',
        '../compiled_resources2.gyp:page_visibility',
        'appearance_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'fonts_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_send',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'home_url_input',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_indicator_behavior',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_pref_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):settings_private',
        '../controls/compiled_resources2.gyp:pref_control_behavior',
        'appearance_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
