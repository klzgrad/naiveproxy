#!/usr/bin/env python
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Fails if a try job increases binary size unexpectedly."""

import argparse
import sys


_MAX_UNNOTICED_INCREASE = 16 * 1024


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--author', help='CL author')
  parser.add_argument('--resource-sizes-diff',
                      help='Path to resource sizes diff produced by '
                           '"diagnose_bloat.py diff sizes".')
  args = parser.parse_args()

  # Last line looks like:
  # MonochromePublic.apk_Specifics normalized apk size=1234
  with open(args.resource_sizes_diff) as f:
    last_line = f.readlines()[-1]
    size_delta = int(last_line.partition('=')[2])

  is_roller = '-autoroll' in args.author

  # Useful for bot debugging to have these printed out:
  print 'Is Roller:', is_roller
  print 'Increase:', size_delta

  if size_delta > _MAX_UNNOTICED_INCREASE and not is_roller:
    # Failure message printed to stderr, so flush first.
    sys.stdout.flush()
    failure_message = """

Binary size increase is non-trivial (where "non-trivial" means the normalized \
size increased by more than {} bytes).

Please look at the symbol diffs from the "Show Resource Sizes Diff" and the \
"Show Supersize Diff" bot steps. Try and understand the growth and see if it \
can be mitigated. There is guidance at:

https://chromium.googlesource.com/chromium/src/+/master/docs/speed/apk_size_regressions.md#Debugging-Apk-Size-Increase

If the growth is expected / justified, then you can bypass this bot failure by \
adding "Binary-Size: $JUSTIFICATION" to your commit description. Here are some \
examples:

Binary-Size: Increase is due to translations and so cannot be avoided.
Binary-Size: Increase is due to new images, which are already optimally encoded.
Binary-Size: Increase is temporary due to a "new way" / "old way" refactoring.
    It should go away once the "old way" is removed.
Binary-Size: Increase is temporary and will be reverted before next branch cut.
Binary-Size: Increase needed to reduce RAM of a common user flow.
Binary-Size: Increase needed to reduce runtime of a common user flow.
Binary-Size: Increase needed to implement a feature, and I've already spent a
    non-trivial amount of time trying to reduce its size.
""".format(_MAX_UNNOTICED_INCREASE)
    # Make blank lines not blank prevent them from being stripped.
    # https://crbug.com/855671
    failure_message.replace('\n\n', '\n.\n')
    sys.exit(failure_message)


if __name__ == '__main__':
  main()
