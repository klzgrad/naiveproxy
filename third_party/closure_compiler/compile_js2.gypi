# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'type': 'none',

  'variables': {
    'CLOSURE_DIR': '<(DEPTH)/third_party/closure_compiler',
    'EXTERNS_GYP': '<(CLOSURE_DIR)/externs/compiled_resources2.gyp',
    'INTERFACES_GYP': '<(CLOSURE_DIR)/interfaces/compiled_resources2.gyp',

    'default_source_file': '<(_target_name).js',
    'source_files%': ['<(default_source_file)'],
    'extra_inputs%': [],

    'includes': ['closure_args.gypi'],
  },

  'sources': ['<@(source_files)'],

  'all_dependent_settings': {
    'sources': ['<@(source_files)'],
  },

  'actions': [
    {
      'action_name': 'compile_js',

      # This action optionally takes these arguments:
      # - sources: a list of all of the source files to be compiled.
      #            If sources is undefined, a default of ['<(_target_name).js']
      #            is created (this probably suffices for many targets).
      # - out_file: a file where the compiled output is written to. The default
      #             is gen/closure/<path to |target_name|>/|target_name|.js.
      # - script_args: additional arguments to pass to compile2.py.
      # - closure_args: additional arguments to pass to the Closure compiler.
      # - disabled_closure_args: additional arguments dealing with the
      #                          strictness of compilation; Non-strict
      #                          defaults are provided that can be overriden.
      'variables': {
        'target_path': '<!(python <(CLOSURE_DIR)/build/outputs.py <(default_source_file))',
        'out_file%': '<(SHARED_INTERMEDIATE_DIR)/closure/<(target_path)',
        # TODO(dbeam): remove when no longer used from remoting/.
        'script_args%': [],
        'closure_args%': '<(default_closure_args)',
        'disabled_closure_args%': '<(default_disabled_closure_args)',
      },

      'inputs': [
        '<(CLOSURE_DIR)/build/outputs.py',
        '<(CLOSURE_DIR)/closure_args.gypi',
        '<(CLOSURE_DIR)/compile2.py',
        '<(CLOSURE_DIR)/compile_js2.gypi',
        '<(CLOSURE_DIR)/compiler/compiler.jar',
        '<(CLOSURE_DIR)/include_js.gypi',
        '<(CLOSURE_DIR)/processor.py',
        '>@(_sources)',
        # When converting to GN, write the paths to additional inputs in a GN
        # depfile file instead.
        '<@(extra_inputs)',
      ],

      'outputs': ['<(out_file)'],

      'action': [
        'python',
        '<(CLOSURE_DIR)/compile2.py',
        '<@(script_args)',
        '>@(_sources)',
        '--out_file', '<(out_file)',
        '--closure_args', '<@(closure_args)', '<@(disabled_closure_args)',
        # '--verbose' # for make glorious log spam of Closure compiler.
      ],

      'message': 'Compiling <(target_path)',
    },
  ],
}
