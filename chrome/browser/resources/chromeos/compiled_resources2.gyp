# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'bluetooth_dialog_host',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):bluetooth',
        '<(EXTERNS_GYP):bluetooth_private',
        '<(INTERFACES_GYP):bluetooth_interface',
        '<(INTERFACES_GYP):bluetooth_private_interface',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'internet_config_dialog',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_components/chromeos/network/compiled_resources2.gyp:network_config',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):chrome_send',
        '<(EXTERNS_GYP):networking_private',
        '<(INTERFACES_GYP):networking_private_interface',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'internet_detail_dialog',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):chrome_send',
        '<(EXTERNS_GYP):networking_private',
        '<(INTERFACES_GYP):networking_private_interface',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
