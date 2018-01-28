# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'route_details',
      'dependencies': [
        'extension_view_wrapper/compiled_resources2.gyp:extension_view_wrapper',
        '../../compiled_resources2.gyp:media_router_data',
        '../../compiled_resources2.gyp:media_router_ui_interface',
        '../route_controls/compiled_resources2.gyp:route_controls',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
