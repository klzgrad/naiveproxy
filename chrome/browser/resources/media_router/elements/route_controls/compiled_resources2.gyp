# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'route_controls',
      'dependencies': [
        '../../compiled_resources2.gyp:media_router_browser_api',
        '../../compiled_resources2.gyp:media_router_data',
        '../../compiled_resources2.gyp:media_router_ui_interface',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'route_controls_interface',
      'dependencies': [
        '../../compiled_resources2.gyp:media_router_data',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
