# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'android_apps_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'android_apps_page',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):settings_private',
        'android_apps_browser_proxy',
      ],
     'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'android_apps_subpage',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_without_ink',
        'android_apps_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
