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
            'googlemock_dir': 'googletest/googlemock',
          }],
          ['crashpad_dependencies=="external"', {
            'googlemock_dir': '../../../../gmock',
          }],
        ],
      },
    }],
  ],
  'target_defaults': {
    # Google Mock relies heavily on objects with static storage duration.
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
      'target_name': 'googlemock',
      'type': 'static_library',
      'dependencies': [
        'googletest.gyp:googletest',
      ],
      'include_dirs': [
        '<(googlemock_dir)',
        '<(googlemock_dir)/include',
      ],
      'sources': [
        '<(googlemock_dir)/include/gmock/gmock-actions.h',
        '<(googlemock_dir)/include/gmock/gmock-cardinalities.h',
        '<(googlemock_dir)/include/gmock/gmock-function-mocker.h',
        '<(googlemock_dir)/include/gmock/gmock-generated-actions.h',
        '<(googlemock_dir)/include/gmock/gmock-matchers.h',
        '<(googlemock_dir)/include/gmock/gmock-more-actions.h',
        '<(googlemock_dir)/include/gmock/gmock-more-matchers.h',
        '<(googlemock_dir)/include/gmock/gmock-nice-strict.h',
        '<(googlemock_dir)/include/gmock/gmock-spec-builders.h',
        '<(googlemock_dir)/include/gmock/gmock.h',
        '<(googlemock_dir)/include/gmock/internal/custom/gmock-generated-actions.h',
        '<(googlemock_dir)/include/gmock/internal/custom/gmock-matchers.h',
        '<(googlemock_dir)/include/gmock/internal/custom/gmock-port.h',
        '<(googlemock_dir)/include/gmock/internal/custom/gmock-pp.h',
        '<(googlemock_dir)/include/gmock/internal/gmock-generated-internal-utils.h',
        '<(googlemock_dir)/include/gmock/internal/gmock-internal-utils.h',
        '<(googlemock_dir)/include/gmock/internal/gmock-port.h',
        '<(googlemock_dir)/src/gmock-all.cc',
        '<(googlemock_dir)/src/gmock-cardinalities.cc',
        '<(googlemock_dir)/src/gmock-internal-utils.cc',
        '<(googlemock_dir)/src/gmock-matchers.cc',
        '<(googlemock_dir)/src/gmock-spec-builders.cc',
        '<(googlemock_dir)/src/gmock.cc',
      ],
      'sources!': [
        '<(googlemock_dir)/src/gmock-all.cc',
      ],

      'direct_dependent_settings': {
        'include_dirs': [
          '<(googlemock_dir)/include',
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
        'googletest.gyp:googletest',
      ],
    },
    {
      'target_name': 'googlemock_main',
      'type': 'static_library',
      'dependencies': [
        'googlemock',
        'googletest.gyp:googletest',
      ],
      'sources': [
        '<(googlemock_dir)/src/gmock_main.cc',
      ],
    },
    {
      'target_name': 'googlemock_test_executable',
      'type': 'none',
      'dependencies': [
        'googlemock',
        'googletest.gyp:googletest',
      ],
      'direct_dependent_settings': {
        'type': 'executable',
        'include_dirs': [
          '<(googlemock_dir)',
        ],
      },
      'export_dependent_settings': [
        'googlemock',
        'googletest.gyp:googletest',
      ],
    },
    {
      'target_name': 'gmock_all_test',
      'dependencies': [
        'googlemock_test_executable',
        'googlemock_main',
      ],
      'include_dirs': [
        'googletest/googletest',
      ],
      'sources': [
        '<(googlemock_dir)/test/gmock-actions_test.cc',
        '<(googlemock_dir)/test/gmock-cardinalities_test.cc',
        '<(googlemock_dir)/test/gmock-function-mocker_test.cc',
        '<(googlemock_dir)/test/gmock-generated-actions_test.cc',
        '<(googlemock_dir)/test/gmock-generated-matchers_test.cc',
        '<(googlemock_dir)/test/gmock-internal-utils_test.cc',
        '<(googlemock_dir)/test/gmock-matchers_test.cc',
        '<(googlemock_dir)/test/gmock-more-actions_test.cc',
        '<(googlemock_dir)/test/gmock-nice-strict_test.cc',
        '<(googlemock_dir)/test/gmock-port_test.cc',
        '<(googlemock_dir)/test/gmock-pp-string_test.cc',
        '<(googlemock_dir)/test/gmock-pp_test.cc',
        '<(googlemock_dir)/test/gmock-spec-builders_test.cc',
        '<(googlemock_dir)/test/gmock_test.cc',
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
      'target_name': 'gmock_link_test',
      'dependencies': [
        'googlemock_test_executable',
        'googlemock_main',
      ],
      'sources': [
        '<(googlemock_dir)/test/gmock_link_test.cc',
        '<(googlemock_dir)/test/gmock_link_test.h',
        '<(googlemock_dir)/test/gmock_link2_test.cc',
      ],
    },
    {
      'target_name': 'gmock_stress_test',
      'dependencies': [
        'googlemock_test_executable',
      ],
      'sources': [
        '<(googlemock_dir)/test/gmock_stress_test.cc',
      ],
    },
    {
      'target_name': 'googlemock_all_tests',
      'type': 'none',
      'dependencies': [
        'gmock_all_test',
        'gmock_link_test',
        'gmock_stress_test',
      ],
    },
  ],
}
