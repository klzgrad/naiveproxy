# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'settings_main',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-a11y-announcer/compiled_resources2.gyp:iron-a11y-announcer-extracted',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '../compiled_resources2.gyp:route',
        '../compiled_resources2.gyp:search_settings',
        '../about_page/compiled_resources2.gyp:about_page',
        '../basic_page/compiled_resources2.gyp:basic_page',
        '../settings_page/compiled_resources2.gyp:main_page_behavior',
        '../compiled_resources2.gyp:page_visibility',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
