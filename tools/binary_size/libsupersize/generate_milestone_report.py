#!/usr/bin/env python
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generate report files to compare milestones.

Size files are located in a Google Cloud Storage bucket for various Chrome
versions. This script generates various HTML report files that compare two
milestones with the same CPU and APK.

Desired CPUs, APKs, and milestone versions are set in constants below.
The script check what HTML report files have already been uploaded to the GCS
bucket, then works on generating the remaining desired files.

Size files are fetched by streaming them from the source bucket,
then the html_report module handles creating a report file to diff two size
files. Reports are saved to a local directory, and once all reports are
created they can be uploaded to the destination bucket.

Reports can be uploaded automatically with the --sync flag. Otherwise, they
can be uploaded at a later point.
"""

import argparse
import codecs
import collections
import cStringIO
import errno
import itertools
import json
import logging
import multiprocessing
import os
import re
import subprocess

import html_report


PUSH_URL = 'gs://chrome-supersize/milestones/'
REPORT_URL_TEMPLATE = '{cpu}/{apk}/report_{version1}_{version2}.ndjson'

DESIRED_CPUS = ['arm', 'arm_64']
DESIRED_APKS = ['Monochrome.apk', 'ChromeModern.apk']
# Versions are manually gathered from
# https://omahaproxy.appspot.com/history?os=android&channel=beta
# The latest version from each milestone was chosen, for the last 9 milestones.
DESIRED_VERSION = [
  '60.0.3112.116',
  '61.0.3163.98',
  '62.0.3202.84',
  '63.0.3239.111',
  '64.0.3282.137',
  '65.0.3325.85',
  '66.0.3359.158',
  '67.0.3396.87',
  '68.0.3440.70',
]


class Report(collections.namedtuple(
  'Report', ['cpu', 'apk', 'version1', 'version2'])):
  PUSH_URL_REGEX = re.compile((PUSH_URL + REPORT_URL_TEMPLATE).format(
    cpu=r'(?P<cpu>[\w.]+)',
    apk=r'(?P<apk>[\w.]+)',
    version1=r'(?P<version1>[\w.]+)',
    version2=r'(?P<version2>[\w.]+)'
  ))

  @classmethod
  def FromUrl(cls, url):
    match = cls.PUSH_URL_REGEX.match(url)
    if match:
      return cls(
        match.group('cpu'),
        match.group('apk'),
        match.group('version1'),
        match.group('version2'),
      )
    else:
      return None


def _FetchExistingMilestoneReports():
  milestones = subprocess.check_output(['gsutil.py', 'ls', '-r', PUSH_URL])
  for path in milestones.splitlines()[1:]:
    report = Report.FromUrl(path)
    if report:
      yield report


def _FetchSizeFile(path):
  return cStringIO.StringIO(subprocess.check_output(['gsutil.py', 'cat', path]))


def _PossibleReportFiles():
  cpu_and_apk_combos = list(itertools.product(DESIRED_CPUS, DESIRED_APKS))
  for i, version1 in enumerate(DESIRED_VERSION):
    for version2 in DESIRED_VERSION[i + 1:]:
      for cpu, apk in cpu_and_apk_combos:
        yield Report(cpu, apk, version1, version2)


def _SetPushedReports(directory):
  outpath = os.path.join(directory, 'milestones.json')
  with codecs.open(outpath, 'w', encoding='ascii') as out_file:
    pushed_reports_obj = {
      'pushed': {
        'cpu': DESIRED_CPUS,
        'apk': DESIRED_APKS,
        'version': DESIRED_VERSION,
      },
    }
    json.dump(pushed_reports_obj, out_file)

def _GetReportPaths(directory, template, report):
  report_dict = report._asdict()
  before_size_path = template.format(version=report.version1,
                                                 **report_dict)
  after_size_path = template.format(version=report.version2,
                                              **report_dict)

  out_rel = os.path.join(directory, REPORT_URL_TEMPLATE.format(**report_dict))
  out_abs = os.path.abspath(out_rel)

  return (before_size_path, after_size_path, out_abs)


def _BuildReport(paths):
  before_size_path, after_size_path, outpath = paths
  try:
    os.makedirs(os.path.dirname(outpath))
  except OSError as e:
    if e.errno != errno.EEXIST:
      raise

  after_file = _FetchSizeFile(after_size_path)
  before_file = _FetchSizeFile(before_size_path)
  with codecs.open(outpath, 'w', encoding='ascii') as out_file:
    html_report.BuildReport(
      out_file,
      size_file=(after_size_path, after_file),
      before_size_file=(before_size_path, before_file),
      all_symbols=True,
    )
  after_file.close()
  before_file.close()
  return outpath


def _BuildReports(directory, bucket):
  try:
    if os.listdir(directory):
      raise Exception('Directory must be empty')
  except OSError as e:
    if e.errno == errno.ENOENT:
      os.makedirs(directory)
    else:
      raise

  # GCS URL template used to get size files.
  template = bucket + '/{version}/{cpu}/{apk}.size'

  desired_reports = _PossibleReportFiles()
  existing_reports = set(_FetchExistingMilestoneReports())

  missing_reports = [s for s in desired_reports if s not in existing_reports]
  paths = (_GetReportPaths(directory, template, r) for r in missing_reports)

  processes = min(len(missing_reports), multiprocessing.cpu_count())
  pool = multiprocessing.Pool(processes=processes)

  for path in pool.imap_unordered(_BuildReport, paths):
    logging.info('Saved %s', path)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('directory',
                      help='Directory to save report files to.')
  parser.add_argument('--size-file-bucket', required=True,
                      help='GCS bucket to find size files in.')
  parser.add_argument('--sync', action='store_true',
                      help='Sync data files to GCS.')
  parser.add_argument('-v',
                      '--verbose',
                      default=0,
                      action='count',
                      help='Verbose level (multiple times for more)')

  args = parser.parse_args()
  logging.basicConfig(level=logging.WARNING - args.verbose * 10,
                      format='%(levelname).1s %(relativeCreated)6d %(message)s')

  size_file_bucket = args.size_file_bucket
  if not size_file_bucket.startswith('gs://'):
    parser.error('Size file bucket must be located in Google Cloud Storage.')
  elif size_file_bucket.endswith('/'):
    # Remove trailing slash
    size_file_bucket = size_file_bucket[:-1]

  _BuildReports(args.directory, size_file_bucket)
  _SetPushedReports(args.directory)
  logging.warning('Reports saved to %s', args.directory)
  cmd = ['gsutil.py', '-m', 'rsync', '-r', args.directory, PUSH_URL]
  if args.sync:
    subprocess.check_call(cmd)
  else:
    logging.warning('Sync files by running: \n%s', ' '.join(cmd))


if __name__ == '__main__':
  main()
