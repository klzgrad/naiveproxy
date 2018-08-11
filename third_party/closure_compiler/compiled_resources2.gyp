# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Add your directory-specific .gyp file to this list for it to be continuously
# typechecked on the builder:
# http://build.chromium.org/p/chromium.fyi/builders/Closure%20Compilation%20Linux
#
# Also, see our guide to Closure compilation in chrome:
# https://chromium.googlesource.com/chromium/src/+/master/docs/closure_compilation.md
{
  'targets': [
    {
      'target_name': 'compiled_resources2',
      'type': 'none',
      'dependencies': [
        '<(DEPTH)/chrome/browser/resources/chromeos/braille_ime/compiled_resources2.gyp:*',
        '<(DEPTH)/chrome/browser/resources/chromeos/compiled_resources2.gyp:*',
        '<(DEPTH)/chrome/browser/resources/chromeos/login/compiled_resources2.gyp:*',
        '<(DEPTH)/chrome/browser/resources/chromeos/multidevice_setup/compiled_resources2.gyp:*',
        '<(DEPTH)/chrome/browser/resources/chromeos/network_ui/compiled_resources2.gyp:*',
        '<(DEPTH)/chrome/browser/resources/chromeos/select_to_speak/compiled_resources2.gyp:*',
        '<(DEPTH)/chrome/browser/resources/chromeos/switch_access/compiled_resources2.gyp:*',
        '<(DEPTH)/chrome/browser/resources/chromeos/sys_internals/compiled_resources2.gyp:*',
        '<(DEPTH)/ui/webui/resources/cr_components/compiled_resources2.gyp:*',
        '<(DEPTH)/ui/webui/resources/cr_elements/compiled_resources2.gyp:*',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:*',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:*',
      ],
    },
  ]
}
