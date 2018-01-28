# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'select_to_speak',
      'dependencies': [
	'externs',
	'<(EXTERNS_GYP):accessibility_private',
	'<(EXTERNS_GYP):automation',
	'<(EXTERNS_GYP):chrome_extensions',
       ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'select_to_speak_options',
      'dependencies': [
	'externs',
	'<(EXTERNS_GYP):accessibility_private',
	'<(EXTERNS_GYP):automation',
	'<(EXTERNS_GYP):chrome_extensions',
       ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'externs',
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
