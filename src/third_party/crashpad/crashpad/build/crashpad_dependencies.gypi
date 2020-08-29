# Copyright 2015 The Crashpad Authors. All rights reserved.
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
  # Crashpad’s GYP build can obtain dependencies in two different ways, directed
  # by the crashpad_standalone GYP variable. It may have these values:
  #   standalone
  #     A “standalone” Crashpad build, where the dependencies are in the
  #     Crashpad tree. third_party/mini_chromium and third_party/googletest
  #     provide the base and Google Test libraries.
  #   external
  #     A build with external dependencies. mini_chromium provides the base
  #     library, but it’s located outside of the Crashpad tree, as is Google
  #     Test.
  #
  # In order for Crashpad’s .gyp files to reference the correct versions
  # depending on how dependencies are being provided, include this .gypi file
  # and reference the crashpad_dependencies variable.
  #
  # Note that Crashpad’s in-Chromium build uses GN instead of GYP, and
  # Chromium’s GN build configures Crashpad to use Chromium’s own base library
  # and its copy of the Google Test library.

  'variables': {
    # When with external dependencies, build/gyp_crashpad.py sets
    # crashpad_dependencies to "external", and this % assignment will not
    # override it.
    'crashpad_dependencies%': 'standalone',
  },
}
