# copyright 2017 the chromium authors. all rights reserved.
# use of this source code is governed by a bsd-style license that can be
# found in the license file.

{
  'targets': [
    {
      'target_name': 'webview_tag',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    }
  ]
}
