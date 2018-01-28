#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

_BUILD_ANDROID = os.path.join(os.path.dirname(__file__), os.pardir)
sys.path.append(_BUILD_ANDROID)
from pylib.constants import host_paths

sys.path.append(os.path.join(host_paths.DIR_SOURCE_ROOT, 'build'))
import find_depot_tools  # pylint: disable=import-error,unused-import
import download_from_google_storage

CURRENT_MILESTONE = '61'
DEFAULT_BUCKET = 'gs://chromium-android-tools/apks'
DEFAULT_DOWNLOAD_PATH = os.path.join(os.path.dirname(__file__), 'apks')
DEFAULT_BUILDER = 'Android_Builder'
DEFAULT_APK = 'MonochromePublic.apk'


def MaybeDownloadApk(builder, milestone, apk, download_path, bucket):
  """Returns path to the downloaded APK or None if not found."""
  apk_path = os.path.join(download_path, builder, milestone, apk)
  sha1_path = apk_path + '.sha1'
  base_url = os.path.join(bucket, builder, milestone)
  if os.path.exists(apk_path):
    print '%s already exists' % apk_path
    return apk_path
  elif not os.path.exists(sha1_path):
    print 'Skipping %s, file not found' % sha1_path
    return None
  else:
    download_from_google_storage.download_from_google_storage(
        input_filename=sha1_path,
        sha1_file=sha1_path,
        base_url=base_url,
        gsutil=download_from_google_storage.Gsutil(
            download_from_google_storage.GSUTIL_DEFAULT_PATH),
        num_threads=1,
        directory=False,
        recursive=False,
        force=False,
        output=apk_path,
        ignore_errors=False,
        verbose=True,
        auto_platform=False,
        extract=False)
    return apk_path


def main():
  argparser = argparse.ArgumentParser(
      description='Utility for downloading archived APKs used for measuring '
                  'per-milestone patch size growth.',
      formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  argparser.add_argument('--download-path', default=DEFAULT_DOWNLOAD_PATH,
                         help='Directory to store downloaded APKs.')
  argparser.add_argument('--milestone', default=CURRENT_MILESTONE,
                         help='Download reference APK for this milestone.')
  argparser.add_argument('--apk', default=DEFAULT_APK, help='APK name.')
  argparser.add_argument('--builder', default=DEFAULT_BUILDER,
                         help='Builder name.')
  argparser.add_argument('--bucket', default=DEFAULT_BUCKET,
                         help='Google storage bucket where APK is stored.')
  args = argparser.parse_args()
  MaybeDownloadApk(
      args.builder, args.milestone, args.apk, args.download_path, args.bucket)


if __name__ == '__main__':
  sys.exit(main())
