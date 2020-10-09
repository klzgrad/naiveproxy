# Copyright 2014 The Crashpad Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

{
  'includes': [
    '../../build/crashpad_dependencies.gypi',
  ],
  'conditions': [
    ['1==1', {  # Defer processing until crashpad_dependencies is set
      'variables': {
        'conditions': [
          ['crashpad_dependencies=="standalone"', {
            'googletest_dir': 'googletest/googletest',
          }],
          ['crashpad_dependencies=="external"', {
            'googletest_dir': '../../../../gtest',
          }],
        ],
      },
    }],
  ],
  'target_defaults': {
    # googletest relies heavily on objects with static storage duration.
    'xcode_settings': {
      'WARNING_CFLAGS!': [
        '-Wexit-time-destructors',
      ],
    },
    'cflags!': [
      '-Wexit-time-destructors',
    ],

    'conditions': [
      ['OS=="android" and android_api_level!="" and android_api_level<24', {
        'defines!': [
          # Although many system interfaces are available to 32-bit code with
          # 64-bit off_t at API 21, the routines in <stdio.h> are not until API
          # 24. googletest doesn’t make use of these functions directly, but can
          # reach them indirectly via the C++ standard library. Disable 64-bit
          # off_t prior to API 24 so that these uses can work. Since nothing
          # dependent on the size of off_t should escape googletest’s own API, this
          # should be safe even in a program that otherwise uses a 64-bit off_t.
          '_FILE_OFFSET_BITS=64',
        ],
      }],
    ],
  },

  'targets': [
    {
      'target_name': 'googletest',
      'type': 'static_library',
      'include_dirs': [
        '<(googletest_dir)',
        '<(googletest_dir)/include',
      ],
      'sources': [
        '<(googletest_dir)/include/gtest/gtest-death-test.h',
        '<(googletest_dir)/include/gtest/gtest-matchers.h',
        '<(googletest_dir)/include/gtest/gtest-message.h',
        '<(googletest_dir)/include/gtest/gtest-param-test.h',
        '<(googletest_dir)/include/gtest/gtest-printers.h',
        '<(googletest_dir)/include/gtest/gtest-spi.h',
        '<(googletest_dir)/include/gtest/gtest-test-part.h',
        '<(googletest_dir)/include/gtest/gtest-typed-test.h',
        '<(googletest_dir)/include/gtest/gtest.h',
        '<(googletest_dir)/include/gtest/gtest_pred_impl.h',
        '<(googletest_dir)/include/gtest/gtest_prod.h',
        '<(googletest_dir)/include/gtest/internal/custom/gtest-port.h',
        '<(googletest_dir)/include/gtest/internal/custom/gtest-printers.h',
        '<(googletest_dir)/include/gtest/internal/custom/gtest.h',
        '<(googletest_dir)/include/gtest/internal/gtest-death-test-internal.h',
        '<(googletest_dir)/include/gtest/internal/gtest-filepath.h',
        '<(googletest_dir)/include/gtest/internal/gtest-internal.h',
        '<(googletest_dir)/include/gtest/internal/gtest-param-util-generated.h',
        '<(googletest_dir)/include/gtest/internal/gtest-param-util.h',
        '<(googletest_dir)/include/gtest/internal/gtest-port-arch.h',
        '<(googletest_dir)/include/gtest/internal/gtest-port.h',
        '<(googletest_dir)/include/gtest/internal/gtest-string.h',
        '<(googletest_dir)/include/gtest/internal/gtest-type-util.h',
        '<(googletest_dir)/src/gtest-all.cc',
        '<(googletest_dir)/src/gtest-death-test.cc',
        '<(googletest_dir)/src/gtest-filepath.cc',
        '<(googletest_dir)/src/gtest-internal-inl.h',
        '<(googletest_dir)/src/gtest-matchers.cc',
        '<(googletest_dir)/src/gtest-port.cc',
        '<(googletest_dir)/src/gtest-printers.cc',
        '<(googletest_dir)/src/gtest-test-part.cc',
        '<(googletest_dir)/src/gtest-typed-test.cc',
        '<(googletest_dir)/src/gtest.cc',
      ],
      'sources!': [
        '<(googletest_dir)/src/gtest-all.cc',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(googletest_dir)/include',
        ],
      },
      'conditions': [
        ['crashpad_dependencies=="external"', {
          'include_dirs': [
            '<(googletest_dir)/../..',
          ],
          'defines': [
            'GUNIT_NO_GOOGLE3=1',
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              '<(googletest_dir)/../..',
            ],
            'defines': [
              'GUNIT_NO_GOOGLE3=1',
            ],
          },
        }],
      ],
    },
    {
      'target_name': 'googletest_main',
      'type': 'static_library',
      'dependencies': [
        'googletest',
      ],
      'sources': [
        '<(googletest_dir)/src/gtest_main.cc',
      ],
    },
    {
      'target_name': 'googletest_test_executable',
      'type': 'none',
      'dependencies': [
        'googletest',
      ],
      'direct_dependent_settings': {
        'type': 'executable',
        'include_dirs': [
          '<(googletest_dir)',
        ],
      },
      'export_dependent_settings': [
        'googletest',
      ],
    },
    {
      'target_name': 'gtest_all_test',
      'dependencies': [
        'googletest_test_executable',
        'googletest_main',
      ],
      'sources': [
        '<(googletest_dir)/test/googletest-death-test-test.cc',
        '<(googletest_dir)/test/googletest-filepath-test.cc',
        '<(googletest_dir)/test/googletest-message-test.cc',
        '<(googletest_dir)/test/googletest-options-test.cc',
        '<(googletest_dir)/test/googletest-port-test.cc',
        '<(googletest_dir)/test/googletest-printers-testcc',
        '<(googletest_dir)/test/googletest-test-part-test.cc',
        '<(googletest_dir)/test/gtest-typed-test2_test.cc',
        '<(googletest_dir)/test/gtest-typed-test_test.cc',
        '<(googletest_dir)/test/gtest-typed-test_test.h',
        '<(googletest_dir)/test/gtest_main_unittest.cc',
        '<(googletest_dir)/test/gtest_pred_impl_unittest.cc',
        '<(googletest_dir)/test/gtest_prod_test.cc',
        '<(googletest_dir)/test/gtest_skip_test.cc',
        '<(googletest_dir)/test/gtest_unittest.cc',
        '<(googletest_dir)/test/production.cc',
        '<(googletest_dir)/test/production.h',
      ],
    },
    {
      'target_name': 'gtest_environment_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_environment_test.cc',
      ],
    },
    {
      'target_name': 'gtest_listener_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/googletest-listener-test.cc',
      ],
    },
    {
      'target_name': 'gtest_macro_stack_footprint_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_test_macro_stack_footprint_test.cc',
      ],
    },
    {
      'target_name': 'gtest_no_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_no_test_unittest.cc',
      ],
    },
    {
      'target_name': 'gtest_param_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/googletest-param-test-test.cc',
        '<(googletest_dir)/test/googletest-param-test-test.h',
        '<(googletest_dir)/test/googletest-param-test2-test.cc',
      ],
      'conditions': [
         ['clang!=0', {
          # For googletest/googlemock/test/gmock-matchers_test.cc’s
          # Unstreamable::value_.
          'conditions': [
            ['OS=="mac"', {
              'xcode_settings': {
                'WARNING_CFLAGS': [
                  '-Wno-unused-private-field',
                ],
              },
            }],
            ['OS=="linux" or OS=="android"', {
              'cflags': [
                '-Wno-unused-private-field',
              ],
            }],
          ],
        }],
      ],
    },
    {
      'target_name': 'gtest_premature_exit_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_premature_exit_test.cc',
      ],
    },
    {
      'target_name': 'gtest_repeat_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_repeat_test.cc',
      ],
    },
    {
      'target_name': 'gtest_skip_in_environment_setup_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_skip_in_environment_setup_test.cc',
      ],
    },
    {
      'target_name': 'gtest_sole_header_test',
      'dependencies': [
        'googletest_test_executable',
        'googletest_main',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_sole_header_test.cc',
      ],
    },
    {
      'target_name': 'gtest_stress_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest_stress_test.cc',
      ],
    },
    {
      'target_name': 'gtest_unittest_api_test',
      'dependencies': [
        'googletest_test_executable',
      ],
      'sources': [
        '<(googletest_dir)/test/gtest-unittest-api_test.cc',
      ],
    },
    {
      'target_name': 'googletest_all_tests',
      'type': 'none',
      'dependencies': [
        'gtest_all_test',
        'gtest_environment_test',
        'gtest_listener_test',
        'gtest_macro_stack_footprint_test',
        'gtest_no_test',
        'gtest_param_test',
        'gtest_premature_exit_test',
        'gtest_repeat_test',
        'gtest_skip_in_environment_setup_test',
        'gtest_sole_header_test',
        'gtest_stress_test',
        'gtest_unittest_api_test',
      ],
    },
  ],
}
