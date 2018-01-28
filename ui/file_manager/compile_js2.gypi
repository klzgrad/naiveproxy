# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
 'variables': {
    'closure_args': [
      '<@(default_closure_args)',
      'warning_level=VERBOSE',
    ],
  },
  'includes': ['../../third_party/closure_compiler/compile_js2.gypi'],
}
