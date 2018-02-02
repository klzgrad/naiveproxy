# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'media_router_container',
      'dependencies': [
        'pseudo_sink_search_state',
        '../../compiled_resources2.gyp:externs',
        '../../compiled_resources2.gyp:media_router_browser_api',
        '../../compiled_resources2.gyp:media_router_data',
        '../../elements/issue_banner/compiled_resources2.gyp:issue_banner',
        '../../elements/media_router_header/compiled_resources2.gyp:media_router_header',
        '../../elements/media_router_search_highlighter/compiled_resources2.gyp:media_router_search_highlighter',
        '../../elements/route_details/compiled_resources2.gyp:route_details',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'media_router_container_interface',
      'dependencies': [
        '../../compiled_resources2.gyp:media_router_data',
        '../../elements/media_router_header/compiled_resources2.gyp:media_router_header',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'pseudo_sink_search_state',
      'dependencies': [
        '../../compiled_resources2.gyp:media_router_data',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
