# copyright 2017 the chromium authors. all rights reserved.
# use of this source code is governed by a bsd-style license that can be
# found in the license file.

{
  'targets': [
    {
      'target_name': 'app_installer',
      'dependencies': [
        'cws_widget_container_platform_delegate',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cws_widget_container',
      'dependencies': [
        'app_installer',
        'cws_webview_client',
        'cws_widget_container_error_dialog',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cws_widget_container_error_dialog',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:dialogs',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cws_widget_container_platform_delegate',
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cws_webview_client',
      'dependencies': [
        '../externs/compiled_resources2.gyp:webview_tag',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        '<(EXTERNS_GYP):chrome_extensions',
        'cws_widget_container_platform_delegate',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}

