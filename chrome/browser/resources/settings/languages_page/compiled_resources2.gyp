# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'languages_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_send',
        '<(EXTERNS_GYP):input_method_private',
        '<(EXTERNS_GYP):language_settings_private',
        '<(INTERFACES_GYP):input_method_private_interface',
        '<(INTERFACES_GYP):language_settings_private_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'languages',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:promise_resolver',
        '<(EXTERNS_GYP):input_method_private',
        '<(EXTERNS_GYP):language_settings_private',
        '<(INTERFACES_GYP):input_method_private_interface',
        '<(INTERFACES_GYP):language_settings_private_interface',
        '../prefs/compiled_resources2.gyp:prefs_types',
        '../prefs/compiled_resources2.gyp:prefs',
        'languages_types',
        'languages_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'languages_page',
      'dependencies': [
        '../compiled_resources2.gyp:lifetime_browser_proxy',
        '../compiled_resources2.gyp:route',
        '../settings_page/compiled_resources2.gyp:settings_animated_pages',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-checkbox/compiled_resources2.gyp:paper-checkbox-extracted',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_action_menu/compiled_resources2.gyp:cr_action_menu',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_expand_button/compiled_resources2.gyp:cr_expand_button',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_lazy_render/compiled_resources2.gyp:cr_lazy_render',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_without_ink',
        'languages',
        'languages_types',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'languages_types',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(EXTERNS_GYP):language_settings_private',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'manage_input_methods_page',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-checkbox/compiled_resources2.gyp:paper-checkbox-extracted',
        '<(EXTERNS_GYP):language_settings_private',
        '../prefs/compiled_resources2.gyp:prefs',
        'languages',
        'languages_types',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'add_languages_dialog',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-checkbox/compiled_resources2.gyp:paper-checkbox-extracted',
        '<(DEPTH)/ui/webui/resources/cr_elements/compiled_resources2.gyp:cr_scrollable_behavior',
        'languages',
        'languages_types',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
