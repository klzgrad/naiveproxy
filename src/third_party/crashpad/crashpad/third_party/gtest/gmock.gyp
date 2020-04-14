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
            'gmock_dir': 'gtest/googlemock',
          }],
          ['crashpad_dependencies=="external"', {
            'gmock_dir': '../../../../gmock',
          }],
        ],
      },
    }],
  ],
  'target_defaults': {
    # gmock relies heavily on objects with static storage duration.
    'xcode_settings': {
      'WARNING_CFLAGS!': [
        '-Wexit-time-destructors',
      ],
    },
    'cflags!': [
      '-Wexit-time-destructors',
    ],
  },

  'targets': [
    {
      'target_name': 'gmock',
      'type': 'static_library',
      'dependencies': [
        'gtest.gyp:gtest',
      ],
      'include_dirs': [
        '<(gmock_dir)',
        '<(gmock_dir)/include',
      ],
      'sources': [
        '<(gmock_dir)/include/gmock/gmock-actions.h',
        '<(gmock_dir)/include/gmock/gmock-cardinalities.h',
        '<(gmock_dir)/include/gmock/gmock-function-mocker.h',
        '<(gmock_dir)/include/gmock/gmock-generated-actions.h',
        '<(gmock_dir)/include/gmock/gmock-generated-function-mockers.h',
        '<(gmock_dir)/include/gmock/gmock-generated-matchers.h',
        '<(gmock_dir)/include/gmock/gmock-matchers.h',
        '<(gmock_dir)/include/gmock/gmock-more-actions.h',
        '<(gmock_dir)/include/gmock/gmock-more-matchers.h',
        '<(gmock_dir)/include/gmock/gmock-nice-strict.h',
        '<(gmock_dir)/include/gmock/gmock-spec-builders.h',
        '<(gmock_dir)/include/gmock/gmock.h',
        '<(gmock_dir)/include/gmock/internal/custom/gmock-generated-actions.h',
        '<(gmock_dir)/include/gmock/internal/custom/gmock-matchers.h',
        '<(gmock_dir)/include/gmock/internal/custom/gmock-port.h',
        '<(gmock_dir)/include/gmock/internal/custom/gmock-pp.h',
        '<(gmock_dir)/include/gmock/internal/gmock-generated-internal-utils.h',
        '<(gmock_dir)/include/gmock/internal/gmock-internal-utils.h',
        '<(gmock_dir)/include/gmock/internal/gmock-port.h',
        '<(gmock_dir)/src/gmock-all.cc',
        '<(gmock_dir)/src/gmock-cardinalities.cc',
        '<(gmock_dir)/src/gmock-internal-utils.cc',
        '<(gmock_dir)/src/gmock-matchers.cc',
        '<(gmock_dir)/src/gmock-spec-builders.cc',
        '<(gmock_dir)/src/gmock.cc',
      ],
      'sources!': [
        '<(gmock_dir)/src/gmock-all.cc',
      ],

      'direct_dependent_settings': {
        'include_dirs': [
          '<(gmock_dir)/include',
        ],
        'conditions': [
          ['clang!=0', {
            # The MOCK_METHODn() macros do not specify “override”, which
            # triggers this warning in users: “error: 'Method' overrides a
            # member function but is not marked 'override'
            # [-Werror,-Winconsistent-missing-override]”. Suppress these
            # warnings until https://github.com/google/googletest/issues/533 is
            # fixed.
            'conditions': [
              ['OS=="mac"', {
                'xcode_settings': {
                  'WARNING_CFLAGS': [
                    '-Wno-inconsistent-missing-override',
                  ],
                },
              }],
              ['OS=="linux" or OS=="android"', {
                'cflags': [
                  '-Wno-inconsistent-missing-override',
                ],
              }],
            ],
          }],
        ],
      },
      'export_dependent_settings': [
        'gtest.gyp:gtest',
      ],
    },
    {
      'target_name': 'gmock_main',
      'type': 'static_library',
      'dependencies': [
        'gmock',
        'gtest.gyp:gtest',
      ],
      'sources': [
        '<(gmock_dir)/src/gmock_main.cc',
      ],
    },
    {
      'target_name': 'gmock_test_executable',
      'type': 'none',
      'dependencies': [
        'gmock',
        'gtest.gyp:gtest',
      ],
      'direct_dependent_settings': {
        'type': 'executable',
        'include_dirs': [
          '<(gmock_dir)',
        ],
      },
      'export_dependent_settings': [
        'gmock',
        'gtest.gyp:gtest',
      ],
    },
    {
      'target_name': 'gmock_all_test',
      'dependencies': [
        'gmock_test_executable',
        'gmock_main',
      ],
      'include_dirs': [
        'gtest/googletest',
      ],
      'sources': [
        '<(gmock_dir)/test/gmock-actions_test.cc',
        '<(gmock_dir)/test/gmock-cardinalities_test.cc',
        '<(gmock_dir)/test/gmock-function-mocker_test.cc',
        '<(gmock_dir)/test/gmock-generated-actions_test.cc',
        '<(gmock_dir)/test/gmock-generated-function-mockers_test.cc',
        '<(gmock_dir)/test/gmock-generated-matchers_test.cc',
        '<(gmock_dir)/test/gmock-internal-utils_test.cc',
        '<(gmock_dir)/test/gmock-matchers_test.cc',
        '<(gmock_dir)/test/gmock-more-actions_test.cc',
        '<(gmock_dir)/test/gmock-nice-strict_test.cc',
        '<(gmock_dir)/test/gmock-port_test.cc',
        '<(gmock_dir)/test/gmock-pp-string_test.cc',
        '<(gmock_dir)/test/gmock-pp_test.cc',
        '<(gmock_dir)/test/gmock-spec-builders_test.cc',
        '<(gmock_dir)/test/gmock_test.cc',
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
      'target_name': 'gmock_link_test',
      'dependencies': [
        'gmock_test_executable',
        'gmock_main',
      ],
      'sources': [
        '<(gmock_dir)/test/gmock_link_test.cc',
        '<(gmock_dir)/test/gmock_link_test.h',
        '<(gmock_dir)/test/gmock_link2_test.cc',
      ],
    },
    {
      'target_name': 'gmock_stress_test',
      'dependencies': [
        'gmock_test_executable',
      ],
      'sources': [
        '<(gmock_dir)/test/gmock_stress_test.cc',
      ],
    },
    {
      'target_name': 'gmock_all_tests',
      'type': 'none',
      'dependencies': [
        'gmock_all_test',
        'gmock_link_test',
        'gmock_stress_test',
      ],
    },
  ],
}
