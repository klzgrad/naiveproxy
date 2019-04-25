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
            'gtest_dir': 'gtest/googletest',
          }],
          ['crashpad_dependencies=="external"', {
            'gtest_dir': '../../../../gtest',
          }],
        ],
      },
    }],
  ],
  'target_defaults': {
    # gtest relies heavily on objects with static storage duration.
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
          # 24. gtest doesn’t make use of these functions directly, but can
          # reach them indirectly via the C++ standard library. Disable 64-bit
          # off_t prior to API 24 so that these uses can work. Since nothing
          # dependent on the size of off_t should escape gtest’s own API, this
          # should be safe even in a program that otherwise uses a 64-bit off_t.
          '_FILE_OFFSET_BITS=64',
        ],
      }],
    ],
  },

  'targets': [
    {
      'target_name': 'gtest',
      'type': 'static_library',
      'include_dirs': [
        '<(gtest_dir)',
        '<(gtest_dir)/include',
      ],
      'sources': [
        '<(gtest_dir)/include/gtest/gtest-death-test.h',
        '<(gtest_dir)/include/gtest/gtest-matchers.h',
        '<(gtest_dir)/include/gtest/gtest-message.h',
        '<(gtest_dir)/include/gtest/gtest-param-test.h',
        '<(gtest_dir)/include/gtest/gtest-printers.h',
        '<(gtest_dir)/include/gtest/gtest-spi.h',
        '<(gtest_dir)/include/gtest/gtest-test-part.h',
        '<(gtest_dir)/include/gtest/gtest-typed-test.h',
        '<(gtest_dir)/include/gtest/gtest.h',
        '<(gtest_dir)/include/gtest/gtest_pred_impl.h',
        '<(gtest_dir)/include/gtest/gtest_prod.h',
        '<(gtest_dir)/include/gtest/internal/custom/gtest-port.h',
        '<(gtest_dir)/include/gtest/internal/custom/gtest-printers.h',
        '<(gtest_dir)/include/gtest/internal/custom/gtest.h',
        '<(gtest_dir)/include/gtest/internal/gtest-death-test-internal.h',
        '<(gtest_dir)/include/gtest/internal/gtest-filepath.h',
        '<(gtest_dir)/include/gtest/internal/gtest-internal.h',
        '<(gtest_dir)/include/gtest/internal/gtest-param-util-generated.h',
        '<(gtest_dir)/include/gtest/internal/gtest-param-util.h',
        '<(gtest_dir)/include/gtest/internal/gtest-port-arch.h',
        '<(gtest_dir)/include/gtest/internal/gtest-port.h',
        '<(gtest_dir)/include/gtest/internal/gtest-string.h',
        '<(gtest_dir)/include/gtest/internal/gtest-type-util.h',
        '<(gtest_dir)/src/gtest-all.cc',
        '<(gtest_dir)/src/gtest-death-test.cc',
        '<(gtest_dir)/src/gtest-filepath.cc',
        '<(gtest_dir)/src/gtest-internal-inl.h',
        '<(gtest_dir)/src/gtest-matchers.cc',
        '<(gtest_dir)/src/gtest-port.cc',
        '<(gtest_dir)/src/gtest-printers.cc',
        '<(gtest_dir)/src/gtest-test-part.cc',
        '<(gtest_dir)/src/gtest-typed-test.cc',
        '<(gtest_dir)/src/gtest.cc',
      ],
      'sources!': [
        '<(gtest_dir)/src/gtest-all.cc',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(gtest_dir)/include',
        ],
      },
      'conditions': [
        ['crashpad_dependencies=="external"', {
          'include_dirs': [
            '<(gtest_dir)/../..',
          ],
          'defines': [
            'GUNIT_NO_GOOGLE3=1',
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              '<(gtest_dir)/../..',
            ],
            'defines': [
              'GUNIT_NO_GOOGLE3=1',
            ],
          },
        }],
      ],
    },
    {
      'target_name': 'gtest_main',
      'type': 'static_library',
      'dependencies': [
        'gtest',
      ],
      'sources': [
        '<(gtest_dir)/src/gtest_main.cc',
      ],
    },
    {
      'target_name': 'gtest_test_executable',
      'type': 'none',
      'dependencies': [
        'gtest',
      ],
      'direct_dependent_settings': {
        'type': 'executable',
        'include_dirs': [
          '<(gtest_dir)',
        ],
      },
      'export_dependent_settings': [
        'gtest',
      ],
    },
    {
      'target_name': 'gtest_all_test',
      'dependencies': [
        'gtest_test_executable',
        'gtest_main',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest-death-test_test.cc',
        '<(gtest_dir)/test/gtest-filepath_test.cc',
        '<(gtest_dir)/test/gtest-linked_ptr_test.cc',
        '<(gtest_dir)/test/gtest-message_test.cc',
        '<(gtest_dir)/test/gtest-options_test.cc',
        '<(gtest_dir)/test/gtest-port_test.cc',
        '<(gtest_dir)/test/gtest-printers_test.cc',
        '<(gtest_dir)/test/gtest-test-part_test.cc',
        '<(gtest_dir)/test/gtest-typed-test2_test.cc',
        '<(gtest_dir)/test/gtest-typed-test_test.cc',
        '<(gtest_dir)/test/gtest-typed-test_test.h',
        '<(gtest_dir)/test/gtest_main_unittest.cc',
        '<(gtest_dir)/test/gtest_pred_impl_unittest.cc',
        '<(gtest_dir)/test/gtest_prod_test.cc',
        '<(gtest_dir)/test/gtest_skip_test.cc',
        '<(gtest_dir)/test/gtest_unittest.cc',
        '<(gtest_dir)/test/production.cc',
        '<(gtest_dir)/test/production.h',
      ],
    },
    {
      'target_name': 'gtest_environment_test',
      'dependencies': [
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest_environment_test.cc',
      ],
    },
    {
      'target_name': 'gtest_listener_test',
      'dependencies': [
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest-listener_test.cc',
      ],
    },
    {
      'target_name': 'gtest_no_test',
      'dependencies': [
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest_no_test_unittest.cc',
      ],
    },
    {
      'target_name': 'gtest_param_test',
      'dependencies': [
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest-param-test2_test.cc',
        '<(gtest_dir)/test/gtest-param-test_test.cc',
        '<(gtest_dir)/test/gtest-param-test_test.h',
      ],
      'conditions': [
         ['clang!=0', {
          # For gtest/googlemock/test/gmock-matchers_test.cc’s
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
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest_premature_exit_test.cc',
      ],
    },
    {
      'target_name': 'gtest_repeat_test',
      'dependencies': [
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest_repeat_test.cc',
      ],
    },
    {
      'target_name': 'gtest_sole_header_test',
      'dependencies': [
        'gtest_test_executable',
        'gtest_main',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest_sole_header_test.cc',
      ],
    },
    {
      'target_name': 'gtest_stress_test',
      'dependencies': [
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest_stress_test.cc',
      ],
    },
    {
      'target_name': 'gtest_unittest_api_test',
      'dependencies': [
        'gtest_test_executable',
      ],
      'sources': [
        '<(gtest_dir)/test/gtest-unittest-api_test.cc',
      ],
    },
    {
      'target_name': 'gtest_all_tests',
      'type': 'none',
      'dependencies': [
        'gtest_all_test',
        'gtest_environment_test',
        'gtest_listener_test',
        'gtest_no_test',
        'gtest_param_test',
        'gtest_premature_exit_test',
        'gtest_repeat_test',
        'gtest_sole_header_test',
        'gtest_stress_test',
        'gtest_unittest_api_test',
      ],
    },
  ],
}
