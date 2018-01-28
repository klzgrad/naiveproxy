# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'includes': [
    'trace_viewer.gypi',
  ],
  'targets': [
    {
      'target_name': 'generate_about_tracing',
      'type': 'none',
      'actions': [
        {
          'action_name': 'generate_about_tracing',
          'script_name': 'bin/generate_about_tracing_contents',
          'inputs': [
            '<@(tracing_files)',
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/content/browser/tracing/about_tracing.js',
            '<(SHARED_INTERMEDIATE_DIR)/content/browser/tracing/about_tracing.html'
          ],
          'action': ['python', '<@(_script_name)',
                     '--outdir', '<(SHARED_INTERMEDIATE_DIR)/content/browser/tracing']
        }
      ]
    }
  ]
}
