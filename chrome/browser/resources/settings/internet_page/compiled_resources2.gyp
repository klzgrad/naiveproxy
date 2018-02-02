# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'internet_page',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '../settings_page/compiled_resources2.gyp:settings_animated_pages',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        '<(EXTERNS_GYP):chrome_send',
        '<(EXTERNS_GYP):management',
        '<(EXTERNS_GYP):networking_private',
        '<(INTERFACES_GYP):networking_private_interface',
        'internet_config',
	'internet_page_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'internet_page_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'internet_config',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '<(DEPTH)/ui/webui/resources/cr_components/chromeos/network/compiled_resources2.gyp:network_config',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_dialog/compiled_resources2.gyp:cr_dialog',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):networking_private',
        '<(INTERFACES_GYP):networking_private_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'internet_detail_page',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):chrome_send',
        '<(EXTERNS_GYP):networking_private',
        '<(INTERFACES_GYP):networking_private_interface',
        'tether_connection_dialog',
	'internet_page_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'internet_known_networks_page',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_action_menu/compiled_resources2.gyp:cr_action_menu',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(EXTERNS_GYP):networking_private',
        '<(INTERFACES_GYP):networking_private_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'internet_subpage',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):networking_private',
        '<(INTERFACES_GYP):networking_private_interface',
	'internet_page_browser_proxy',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'network_proxy_section',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '../controls/compiled_resources2.gyp:settings_checkbox',
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '<(DEPTH)/ui/webui/resources/cr_components/chromeos/network/compiled_resources2.gyp:network_proxy',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_without_ink',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'network_summary',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(INTERFACES_GYP):networking_private_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'network_summary_item',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_network_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(INTERFACES_GYP):networking_private_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'tether_connection_dialog',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_dialog/compiled_resources2.gyp:cr_dialog',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
