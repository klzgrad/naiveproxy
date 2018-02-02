# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'externs',
      'includes': ['../../../../third_party/closure_compiler/include_js.gypi'],
    },
    {
      'target_name': 'media_router',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        'media_router_ui_interface',
        'elements/media_router_header/compiled_resources2.gyp:media_router_header',
        'elements/media_router_container/compiled_resources2.gyp:media_router_container_interface',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'media_router_browser_api',
      'dependencies': [
        'media_router_data',
        '<(EXTERNS_GYP):chrome_send',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'media_router_data',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'media_router_ui_interface',
      'dependencies': [
        'media_router_browser_api',
        'elements/media_router_header/compiled_resources2.gyp:media_router_header',
        'elements/media_router_container/compiled_resources2.gyp:media_router_container_interface',
        'elements/route_controls/compiled_resources2.gyp:route_controls_interface',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
