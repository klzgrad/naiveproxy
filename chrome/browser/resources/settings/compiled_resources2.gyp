# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'extension_control_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_send',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'focus_row_behavior',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_row',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'global_scroll_target_behavior',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        'route',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'lifetime_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_send',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'route',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):metrics_private',
        'page_visibility'
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'page_visibility',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'search_settings',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'settings_resources',
      'type': 'none',
      'dependencies': [
        'a11y_page/compiled_resources2.gyp:*',
        'about_page/compiled_resources2.gyp:*',
        'android_apps_page/compiled_resources2.gyp:*',
        'animation/compiled_resources2.gyp:*',
        'appearance_page/compiled_resources2.gyp:*',
        'basic_page/compiled_resources2.gyp:*',
        'bluetooth_page/compiled_resources2.gyp:*',
        'change_password_page/compiled_resources2.gyp:*',
        'chrome_cleanup_page/compiled_resources2.gyp:*',
        'clear_browsing_data_dialog/compiled_resources2.gyp:*',
        'controls/compiled_resources2.gyp:*',
        'date_time_page/compiled_resources2.gyp:*',
        'default_browser_page/compiled_resources2.gyp:*',
        'device_page/compiled_resources2.gyp:*',
        'downloads_page/compiled_resources2.gyp:*',
        'internet_page/compiled_resources2.gyp:*',
        'languages_page/compiled_resources2.gyp:*',
        'on_startup_page/compiled_resources2.gyp:*',
        'passwords_and_forms_page/compiled_resources2.gyp:*',
        'people_page/compiled_resources2.gyp:*',
        'prefs/compiled_resources2.gyp:*',
        'printing_page/compiled_resources2.gyp:*',
        'privacy_page/compiled_resources2.gyp:*',
        'reset_page/compiled_resources2.gyp:*',
        'search_engines_page/compiled_resources2.gyp:*',
        'search_page/compiled_resources2.gyp:*',
        'settings_main/compiled_resources2.gyp:*',
        'settings_menu/compiled_resources2.gyp:*',
        'settings_page/compiled_resources2.gyp:*',
        'settings_ui/compiled_resources2.gyp:*',
        'site_settings/compiled_resources2.gyp:*',
        'site_settings_page/compiled_resources2.gyp:*',
        'system_page/compiled_resources2.gyp:*',
      ],
    },
  ],
}
