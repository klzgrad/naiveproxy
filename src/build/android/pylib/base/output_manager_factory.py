# Copyright 2017 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


from pylib import constants
from pylib.output import local_output_manager
from pylib.output import remote_output_manager
from pylib.utils import local_utils


def CreateOutputManager(args):
  if args.local_output or not local_utils.IsOnSwarming():
    return local_output_manager.LocalOutputManager(
        output_dir=constants.GetOutDirectory())
  return remote_output_manager.RemoteOutputManager(
      bucket=args.gs_results_bucket)
