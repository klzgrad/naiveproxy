# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(dpapad): Add compile targets for all files, crbug.com/721073.
{
  'targets': [
    {
      'target_name': 'browser_api',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):mime_handler_private',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'gesture_detector',
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'open_pdf_params_parser',
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'pdf_scripting_api',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'viewport_scroller',
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'pdf_resources',
      'type': 'none',
      'dependencies': [
        'elements/viewer-bookmark/compiled_resources2.gyp:*',
        'elements/viewer-error-screen/compiled_resources2.gyp:*',
        'elements/viewer-page-indicator/compiled_resources2.gyp:*',
        'elements/viewer-page-selector/compiled_resources2.gyp:*',
        'elements/viewer-password-screen/compiled_resources2.gyp:*',
      ],
    },
  ],
}
