# Copyright (C) 2013 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
{
  'variables': {
    # Default include directories. Override with your system's include paths or
    # paths to your own implementations.
    'gtest_dir%':     '/usr/include',
    'gtest_src_dir%': '/usr/src/gtest',
  },
  'targets': [
    {
      'target_name': 'main',
      'type': 'static_library',
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/src/gtest-all.cc',
      ],
      'include_dirs': [
        '<(gtest_dir)',
        '<(gtest_src_dir)',
      ],
      'copies': [
        {
          'destination': '<(SHARED_INTERMEDIATE_DIR)/src',
          'files': [
            '<(gtest_src_dir)/src/gtest-all.cc',
            '<(gtest_src_dir)/src/gtest_main.cc',
          ],
        },
      ],
      'direct_dependent_settings': {
        'sources': [
          '<(SHARED_INTERMEDIATE_DIR)/src/gtest_main.cc',
        ],
        'include_dirs': [
          '<(gtest_dir)',
        ],
        'conditions': [
          ['OS == "linux"', {
            'ldflags': [
              '-pthread', # GTest needs to link to pthread on Linux.
            ],
          }],
        ],
      },
    },
  ],
}
