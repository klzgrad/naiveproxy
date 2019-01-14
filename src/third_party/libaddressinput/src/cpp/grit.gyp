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
    'grit_dir%': '<(DEPTH)/../externals/grit',
  },
  'targets': [
    {
      'target_name': 'generated_messages',
      'type': 'none',
      'sources': [
        'res/messages.grd',
      ],
      'actions': [
        {
          'action_name': 'generate_messages',
          'inputs': [
            '<(grit_dir)/grit.py',
            'res/messages.grd',
            'res/messages.grdp',
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/en_messages.cc',
            '<(SHARED_INTERMEDIATE_DIR)/messages.h',
          ],
          'action': [
            'python',
            '<(grit_dir)/grit.py',
            '-i',
            'res/messages.grd',
            'build',
            '-o',
            '<(SHARED_INTERMEDIATE_DIR)',
          ],
        },
      ],
      'all_dependent_settings': {
        'include_dirs': [
          '<(SHARED_INTERMEDIATE_DIR)',
        ],
      },
    },
  ],
}
