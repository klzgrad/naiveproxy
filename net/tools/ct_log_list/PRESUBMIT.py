# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


"""Chromium presubmit script for src/net/tools/ct_log_list."""


def _RunMakeCTLogListTests(input_api, output_api):
  """Runs make_ct_known_logs_list unittests if related files were modified."""
  files = ('net/tools/ct_log_list/make_ct_known_logs_list.py',
           'net/tools/ct_log_list/make_ct_known_logs_list_unittest.py',
           'net/data/ssl/certificate_transparency/log_list.json')
  if not any(f in input_api.LocalPaths() for f in files):
    return []
  test_path = input_api.os_path.join(input_api.PresubmitLocalPath(),
                                     'make_ct_known_logs_list_unittest.py')
  cmd_name = 'make_ct_known_logs_list_unittest'
  cmd = [input_api.python_executable, test_path]
  test_cmd = input_api.Command(
    name=cmd_name,
    cmd=cmd,
    kwargs={},
    message=output_api.PresubmitPromptWarning)
  return input_api.RunTests([test_cmd])


def CheckChangeOnUpload(input_api, output_api):
  return _RunMakeCTLogListTests(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
  return _RunMakeCTLogListTests(input_api, output_api)

