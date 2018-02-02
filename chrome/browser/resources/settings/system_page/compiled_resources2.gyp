# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'system_page_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):chrome_send',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'system_page',
      'dependencies': [
        '../compiled_resources2.gyp:lifetime_browser_proxy',
        'system_page_browser_proxy',
        '<(EXTERNS_GYP):settings_private',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
