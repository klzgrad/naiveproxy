#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Signs and zipaligns APK.

"""

import optparse
import os
import shutil
import sys
import tempfile
import zipfile

# resource_sizes modifies zipfile for zip64 compatibility. See
# https://bugs.python.org/issue14315.
sys.path.append(os.path.join(os.path.dirname(__file__), os.pardir))
import resource_sizes  # pylint: disable=unused-import

from util import build_utils


def JarSigner(key_path, key_name, key_passwd, unsigned_path, signed_path):
  shutil.copy(unsigned_path, signed_path)
  sign_cmd = [
      'jarsigner',
      '-sigalg', 'MD5withRSA',
      '-digestalg', 'SHA1',
      '-keystore', key_path,
      '-storepass', key_passwd,
      signed_path,
      key_name,
    ]
  build_utils.CheckOutput(sign_cmd)


def AlignApk(zipalign_path, unaligned_path, final_path):
  # Note -p will page align native libraries (files ending with .so), but
  # only those that are stored uncompressed.
  align_cmd = [
      zipalign_path,
      '-p',
      '-f',
      ]


  align_cmd += [
      '4',  # 4 bytes
      unaligned_path,
      final_path,
      ]
  build_utils.CheckOutput(align_cmd)


def main(args):
  args = build_utils.ExpandFileArgs(args)

  parser = optparse.OptionParser()
  build_utils.AddDepfileOption(parser)

  parser.add_option('--zipalign-path', help='Path to the zipalign tool.')
  parser.add_option('--unsigned-apk-path', help='Path to input unsigned APK.')
  parser.add_option('--final-apk-path',
      help='Path to output signed and aligned APK.')
  parser.add_option('--key-path', help='Path to keystore for signing.')
  parser.add_option('--key-passwd', help='Keystore password')
  parser.add_option('--key-name', help='Keystore name')

  options, _ = parser.parse_args()

  input_paths = [
    options.unsigned_apk_path,
    options.key_path,
  ]

  input_strings = [
    options.key_name,
    options.key_passwd,
  ]

  build_utils.CallAndWriteDepfileIfStale(
      lambda: FinalizeApk(options),
      options,
      record_path=options.unsigned_apk_path + '.finalize.md5.stamp',
      input_paths=input_paths,
      input_strings=input_strings,
      output_paths=[options.final_apk_path])


def _NormalizeZip(path):
  with tempfile.NamedTemporaryFile(suffix='.zip') as hermetic_signed_apk:
    with zipfile.ZipFile(path, 'r') as zi:
      with zipfile.ZipFile(hermetic_signed_apk, 'w') as zo:
        for info in zi.infolist():
          # Ignore 'extended local file headers'. Python doesn't write them
          # properly (see https://bugs.python.org/issue1742205) which causes
          # zipalign to miscalculate alignment. Since we don't use them except
          # for alignment anyway, we write a stripped file here and let
          # zipalign add them properly later. eLFHs are controlled by 'general
          # purpose bit flag 03' (0x08) so we mask that out.
          info.flag_bits = info.flag_bits & 0xF7

          info.date_time = build_utils.HERMETIC_TIMESTAMP
          zo.writestr(info, zi.read(info.filename))

    shutil.copy(hermetic_signed_apk.name, path)


def FinalizeApk(options):
  with tempfile.NamedTemporaryFile() as signed_apk_path_tmp:
    signed_apk_path = signed_apk_path_tmp.name
    JarSigner(options.key_path, options.key_name, options.key_passwd,
              options.unsigned_apk_path, signed_apk_path)
    # Make the newly added signing files hermetic.
    _NormalizeZip(signed_apk_path)

    AlignApk(options.zipalign_path, signed_apk_path, options.final_apk_path)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
