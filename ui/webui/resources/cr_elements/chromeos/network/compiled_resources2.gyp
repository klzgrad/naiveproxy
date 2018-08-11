# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'cr_network_icon',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'cr_onc_types',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_network_icon_externs',
      'includes': ['../../../../../../third_party/closure_compiler/include_js.gypi'],
    },
    {
      'target_name': 'cr_network_list',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/compiled_resources2.gyp:cr_scrollable_behavior',
        'cr_network_list_types',
        'cr_onc_types',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_network_list_item',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'cr_network_list_types',
        'cr_onc_types',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_network_list_types',
      'dependencies': [
        'cr_onc_types',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_network_select',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        '<(EXTERNS_GYP):networking_private',
        'cr_network_list_types',
        'cr_onc_types',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_onc_types',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        '<(EXTERNS_GYP):networking_private',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
