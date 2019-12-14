#!/usr/bin/env python3
#
# Copyright (c) 2015-2019 The Khronos Group Inc.
# Copyright (c) 2015-2019 Valve Corporation
# Copyright (c) 2015-2019 LunarG, Inc.
# Copyright (c) 2015-2019 Google Inc.
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
#
# Author: Cort Stratton <cort@google.com>
# Author: Jean-Francois Roy <jfroy@google.com>

import argparse
import hashlib
import subprocess
import uuid
import json

def generate(symbol_name, commit_id, output_header_file):
    # Write commit ID to output header file
    with open(output_header_file, "w") as header_file:
         # File Comment
        file_comment = '// *** THIS FILE IS GENERATED - DO NOT EDIT ***\n'
        file_comment += '// See external_revision_generator.py for modifications\n'
        header_file.write(file_comment)
        # Copyright Notice
        copyright = ''
        copyright += '\n'
        copyright += '/***************************************************************************\n'
        copyright += ' *\n'
        copyright += ' * Copyright (c) 2015-2019 The Khronos Group Inc.\n'
        copyright += ' * Copyright (c) 2015-2019 Valve Corporation\n'
        copyright += ' * Copyright (c) 2015-2019 LunarG, Inc.\n'
        copyright += ' * Copyright (c) 2015-2019 Google Inc.\n'
        copyright += ' *\n'
        copyright += ' * Licensed under the Apache License, Version 2.0 (the "License");\n'
        copyright += ' * you may not use this file except in compliance with the License.\n'
        copyright += ' * You may obtain a copy of the License at\n'
        copyright += ' *\n'
        copyright += ' *     http://www.apache.org/licenses/LICENSE-2.0\n'
        copyright += ' *\n'
        copyright += ' * Unless required by applicable law or agreed to in writing, software\n'
        copyright += ' * distributed under the License is distributed on an "AS IS" BASIS,\n'
        copyright += ' * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n'
        copyright += ' * See the License for the specific language governing permissions and\n'
        copyright += ' * limitations under the License.\n'
        copyright += ' *\n'
        copyright += ' * Author: Chris Forbes <chrisforbes@google.com>\n'
        copyright += ' * Author: Cort Stratton <cort@google.com>\n'
        copyright += ' *\n'
        copyright += ' ****************************************************************************/\n'
        header_file.write(copyright)
        # Contents
        contents = '#pragma once\n\n'
        contents += '#define %s "%s"\n' % (symbol_name, commit_id)
        header_file.write(contents)

def get_commit_id_from_git(git_binary, source_dir):
    value = subprocess.check_output([git_binary, "rev-parse", "HEAD"], cwd=source_dir).decode('utf-8').strip()
    return value

def is_sha1(str):
    try: str_as_int = int(str, 16)
    except ValueError: return False
    return len(str) == 40

def get_commit_id_from_file(rev_file):
    with open(rev_file, 'r') as rev_stream:
        rev_contents = rev_stream.read()
        rev_contents_stripped = rev_contents.strip()
        if is_sha1(rev_contents_stripped):
            return rev_contents_stripped;
        # otherwise, SHA1 the entire (unstripped) file contents
        sha1 = hashlib.sha1();
        sha1.update(rev_contents.encode('utf-8'))
        return sha1.hexdigest()

def get_commit_id_from_uuid():
    unique_uuid = str(uuid.uuid4())
    sha1 = hashlib.sha1();
    sha1.update(unique_uuid.encode())
    return sha1.hexdigest()

def get_commit_id_from_json(json_file, json_keys):
    with open(json_file) as json_stream:
        json_data = json.load(json_stream)
    for key in json_keys.split(','):
        if type(json_data) == list:
            json_data = json_data[int(key)]
        else:
            json_data = json_data[key]
    return json_data

def main():
    parser = argparse.ArgumentParser()
    rev_method_group = parser.add_mutually_exclusive_group(required=True)
    rev_method_group.add_argument("--git_dir", metavar="SOURCE_DIR", help="git working copy directory")
    rev_method_group.add_argument("--rev_file", metavar="REVISION_FILE", help="source revision file path (must contain a SHA1 hash")
    rev_method_group.add_argument("--from_uuid", action='store_true', help="base SHA1 on a dynamically generated UUID")
    rev_method_group.add_argument("--json_file", metavar="JSON_FILE", help="path to json file")
    parser.add_argument("-s", "--symbol_name", metavar="SYMBOL_NAME", required=True, help="C symbol name")
    parser.add_argument("-o", "--output_header_file", metavar="OUTPUT_HEADER_FILE", required=True, help="output header file path")
    parser.add_argument("--json_keys", action='store', metavar="JSON_KEYS", help="comma-separated list of keys specifying SHA1 location in root json object for --json_file option")
    args = parser.parse_args()

    if ('json_file' in args) != ('json_keys' in args):
        parser.error('--json_file and --json_keys must be provided together')

    # We can either parse the latest Git commit ID out of the specified repository (preferred where possible),
    # or computing the SHA1 hash of the contents of a file passed on the command line and (where necessary --
    # e.g. when building the layers outside of a Git environment).
    if args.git_dir is not None:
        # Extract commit ID from the specified source directory
        try:
            commit_id = get_commit_id_from_git('git', args.git_dir)
        except WindowsError:
            # Call git.bat on Windows for compatibility.
            commit_id = get_commit_id_from_git('git.bat', args.git_dir)
    elif args.rev_file is not None:
        # Read the commit ID from a file.
        commit_id = get_commit_id_from_file(args.rev_file)
    elif args.json_file is not None:
        commit_id = get_commit_id_from_json(args.json_file, args.json_keys)
    elif args.from_uuid:
        commit_id = get_commit_id_from_uuid()

    if not is_sha1(commit_id):
        raise ValueError("commit ID for " + args.symbol_name + " must be a SHA1 hash.")

    generate(args.symbol_name, commit_id, args.output_header_file)

if __name__ == '__main__':
    main()
