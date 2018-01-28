# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'util',
      'includes': ['../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'jsevalcontext',
      'dependencies': ['util'],
      'includes': ['../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'jstemplate',
      'dependencies': ['jsevalcontext'],
      'includes': ['../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
