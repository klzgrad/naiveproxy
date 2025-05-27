#! /usr/bin/env python3
#
# Copyright 2017 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import argparse
import json
import os
import sys


def merge_shard_results(summary_json, jsons_to_merge):
  """Reads JSON test output from all shards and combines them into one.

  Returns dict with merged test output on success or None on failure. Emits
  annotations.
  """
  try:
    with open(summary_json) as f:
      summary = json.load(f)
  except (IOError, ValueError):
    # TODO(crbug.com/40196155):Re-enable this check after the recipe module
    # chromium_swarming can run it with py3
    # pylint: disable=raise-missing-from
    raise Exception('Summary json cannot be loaded.')

  # Merge all JSON files together. Keep track of missing shards.
  merged = {
    'all_tests': set(),
    'disabled_tests': set(),
    'global_tags': set(),
    'missing_shards': [],
    'per_iteration_data': [],
    'swarming_summary': summary,
    'links': set()
  }
  for index, result in enumerate(summary['shards']):
    if result is None:
      merged['missing_shards'].append(index)
      continue

    # Author note: this code path doesn't trigger convert_to_old_format() in
    # client/swarming.py, which means the state enum is saved in its string
    # name form, not in the number form.
    state = result.get('state')
    if state == 'BOT_DIED':
      print(
          'Shard #%d had a Swarming internal failure' % index, file=sys.stderr)
    elif state == 'EXPIRED':
      print('There wasn\'t enough capacity to run your test', file=sys.stderr)
    elif state == 'TIMED_OUT':
      print('Test runtime exceeded allocated time'
            'Either it ran for too long (hard timeout) or it didn\'t produce '
            'I/O for an extended period of time (I/O timeout)',
            file=sys.stderr)
    elif state != 'COMPLETED':
      print('Invalid Swarming task state: %s' % state, file=sys.stderr)

    json_data, err_msg = load_shard_json(index, result.get('task_id'),
                                         jsons_to_merge)
    if json_data:
      # Set-like fields.
      for key in ('all_tests', 'disabled_tests', 'global_tags', 'links'):
        merged[key].update(json_data.get(key), [])

      # 'per_iteration_data' is a list of dicts. Dicts should be merged
      # together, not the 'per_iteration_data' list itself.
      merged['per_iteration_data'] = merge_list_of_dicts(
          merged['per_iteration_data'], json_data.get('per_iteration_data', []))
    else:
      merged['missing_shards'].append(index)
      print('No result was found: %s' % err_msg, file=sys.stderr)

  # If some shards are missing, make it known. Continue parsing anyway. Step
  # should be red anyway, since swarming.py return non-zero exit code in that
  # case.
  if merged['missing_shards']:
    as_str = ', '.join([str(shard) for shard in merged['missing_shards']])
    print('some shards did not complete: %s' % as_str, file=sys.stderr)
    # Not all tests run, combined JSON summary can not be trusted.
    merged['global_tags'].add('UNRELIABLE_RESULTS')

  # Convert to jsonish dict.
  for key in ('all_tests', 'disabled_tests', 'global_tags', 'links'):
    merged[key] = sorted(merged[key])
  return merged


OUTPUT_JSON_SIZE_LIMIT = 100 * 1024 * 1024  # 100 MB


def load_shard_json(index, task_id, jsons_to_merge):
  """Reads JSON output of the specified shard.

  Args:
    output_dir: The directory in which to look for the JSON output to load.
    index: The index of the shard to load data for, this is for old api.
    task_id: The directory of the shard to load data for, this is for new api.

  Returns: A tuple containing:
    * The contents of path, deserialized into a python object.
    * An error string.
    (exactly one of the tuple elements will be non-None).
  """
  matching_json_files = [
      j for j in jsons_to_merge
      if (os.path.basename(j) == 'output.json' and
          (os.path.basename(os.path.dirname(j)) == str(index) or
           os.path.basename(os.path.dirname(j)) == task_id))]

  if not matching_json_files:
    print('shard %s test output missing' % index, file=sys.stderr)
    return (None, 'shard %s test output was missing' % index)
  if len(matching_json_files) > 1:
    print('duplicate test output for shard %s' % index, file=sys.stderr)
    return (None, 'shard %s test output was duplicated' % index)

  path = matching_json_files[0]

  try:
    filesize = os.stat(path).st_size
    if filesize > OUTPUT_JSON_SIZE_LIMIT:
      print(
          'output.json is %d bytes. Max size is %d' % (filesize,
                                                       OUTPUT_JSON_SIZE_LIMIT),
          file=sys.stderr)
      return (None, 'shard %s test output exceeded the size limit' % index)

    with open(path) as f:
      return (json.load(f), None)
  except (IOError, ValueError, OSError) as e:
    print('Missing or invalid gtest JSON file: %s' % path, file=sys.stderr)
    print('%s: %s' % (type(e).__name__, e), file=sys.stderr)

    return (None, 'shard %s test output was missing or invalid' % index)


def merge_list_of_dicts(left, right):
  """Merges dicts left[0] with right[0], left[1] with right[1], etc."""
  output = []
  for i in range(max(len(left), len(right))):
    left_dict = left[i] if i < len(left) else {}
    right_dict = right[i] if i < len(right) else {}
    merged_dict = left_dict.copy()
    merged_dict.update(right_dict)
    output.append(merged_dict)
  return output


def standard_gtest_merge(
    output_json, summary_json, jsons_to_merge):

  output = merge_shard_results(summary_json, jsons_to_merge)
  with open(output_json, 'w') as f:
    json.dump(output, f)

  return 0


def main(raw_args):
  parser = argparse.ArgumentParser()
  parser.add_argument('--summary-json')
  parser.add_argument('-o', '--output-json', required=True)
  parser.add_argument('jsons_to_merge', nargs='*')

  args = parser.parse_args(raw_args)

  return standard_gtest_merge(
      args.output_json, args.summary_json, args.jsons_to_merge)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
