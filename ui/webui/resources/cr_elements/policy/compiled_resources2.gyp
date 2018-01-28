# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'cr_policy_indicator',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'cr_policy_indicator_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_policy_indicator_behavior',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_policy_pref_behavior',
      'dependencies': [
        '<(EXTERNS_GYP):settings_private',
        'cr_policy_indicator_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_policy_pref_indicator',
      'dependencies': [
        '<(EXTERNS_GYP):settings_private',
        'cr_policy_indicator_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_policy_network_behavior',
      'dependencies': [
        '../chromeos/network/compiled_resources2.gyp:cr_onc_types',
        'cr_policy_indicator_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_policy_network_indicator',
      'dependencies': [
        '../chromeos/network/compiled_resources2.gyp:cr_onc_types',
        'cr_policy_indicator_behavior',
        'cr_policy_network_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_tooltip_icon',
      'dependencies': [],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
