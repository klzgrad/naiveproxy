# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'basic_page',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        '../android_apps_page/compiled_resources2.gyp:android_apps_browser_proxy',
        '../change_password_page/compiled_resources2.gyp:change_password_browser_proxy',
        '../chrome_cleanup_page/compiled_resources2.gyp:chrome_cleanup_proxy',
        '../compiled_resources2.gyp:route',
        '../compiled_resources2.gyp:search_settings',
        '../settings_page/compiled_resources2.gyp:main_page_behavior',
        '../compiled_resources2.gyp:page_visibility',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
