# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'search_page',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '../compiled_resources2.gyp:route',
        '../prefs/compiled_resources2.gyp:prefs',
        '../search_engines_page/compiled_resources2.gyp:search_engines_browser_proxy',
        '../settings_page/compiled_resources2.gyp:settings_animated_pages',
     ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
