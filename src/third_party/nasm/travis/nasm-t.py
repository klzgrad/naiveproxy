#!/usr/bin/python3

import subprocess
import argparse
import difflib
import filecmp
import fnmatch
import json
import sys
import re
import os

fmtr_class = argparse.ArgumentDefaultsHelpFormatter
parser = argparse.ArgumentParser(prog = 'nasm-t.py',
                                 formatter_class=fmtr_class)

parser.add_argument('-d', '--directory',
                    dest = 'dir', default = './travis/test',
                    help = 'Directory with tests')

parser.add_argument('--nasm',
                    dest = 'nasm', default = './nasm',
                    help = 'Nasm executable to use')

parser.add_argument('--hexdump',
                    dest = 'hexdump', default = '/usr/bin/hexdump',
                    help = 'Hexdump executable to use')

sp = parser.add_subparsers(dest = 'cmd')
for cmd in ['run']:
    spp = sp.add_parser(cmd, help = 'Run test cases')
    spp.add_argument('-t', '--test',
                     dest = 'test',
                     help = 'Run the selected test only',
                     required = False)

for cmd in ['list']:
    spp = sp.add_parser(cmd, help = 'List test cases')

for cmd in ['update']:
    spp = sp.add_parser(cmd, help = 'Update test cases with new compiler')
    spp.add_argument('-t', '--test',
                     dest = 'test',
                     help = 'Update the selected test only',
                     required = False)

args = parser.parse_args()

if args.cmd == None:
    parser.print_help()
    sys.exit(1)

def read_stdfile(path):
    with open(path, "rb") as f:
        data = f.read().decode("utf-8").strip("\n")
        f.close()
        return data

#
# Check if descriptor has mandatory fields
def is_valid_desc(desc):
    if desc == None:
        return False
    if 'description' not in desc:
        return false
    return True

#
# Expand ref/id in descriptors array
def expand_templates(desc_array):
    desc_ids = { }
    for d in desc_array:
        if 'id' in d:
            desc_ids[d['id']] = d
    for i, d in enumerate(desc_array):
        if 'ref' in d and d['ref'] in desc_ids:
            ref = desc_ids[d['ref']]
            own = d.copy()
            desc_array[i] = ref.copy()
            for k, v in own.items():
                desc_array[i][k] = v
            del desc_array[i]['id']
    return desc_array

def prepare_desc(desc, basedir, name, path):
    if not is_valid_desc(desc):
        return False
    #
    # Put private fields
    desc['_base-dir'] = basedir
    desc['_json-file'] = name
    desc['_json-path'] = path
    desc['_test-name'] = basedir + os.sep + name[:-5]
    #
    # If no target provided never update
    if 'target' not in desc:
        desc['target'] = []
        desc['update'] = 'false'
    #
    # Which code to expect when nasm finishes
    desc['_wait'] = 0
    if 'error' in desc:
        if desc['error'] == 'expected':
            desc['_wait'] = 1
    #
    # Walk over targets and generate match templates
    # if were not provided yet
    for d in desc['target']:
        if 'output' in d and not 'match' in d:
            d['match'] = d['output'] + ".t"
    return True

def read_json(path):
    desc = None
    try:
        with open(path, "rb") as f:
            try:
                desc = json.loads(f.read().decode("utf-8").strip("\n"))
            except:
                desc = None
            finally:
                f.close()
    except:
        pass
    return desc

def read_desc(basedir, name):
    path = basedir + os.sep + name
    desc = read_json(path)
    desc_array = []
    if type(desc) == dict:
        if prepare_desc(desc, basedir, name, path) == True:
            desc_array += [desc]
    elif type(desc) == list:
        expand_templates(desc)
        for de in desc:
            if prepare_desc(de, basedir, name, path) == True:
                desc_array += [de]
    return desc_array

def collect_test_desc_from_file(path):
    if not fnmatch.fnmatch(path, '*.json'):
        path += '.json'
    basedir = os.path.dirname(path)
    filename = os.path.basename(path)
    return read_desc(basedir, filename)

def collect_test_desc_from_dir(basedir):
    desc_array = []
    if os.path.isdir(basedir):
        for filename in os.listdir(basedir):
            if os.path.isdir(basedir + os.sep + filename):
                desc_array += collect_test_desc_from_dir(basedir + os.sep + filename)
            elif fnmatch.fnmatch(filename, '*.json'):
                desc = read_desc(basedir, filename)
                if desc == None:
                    continue
                desc_array += desc
    return desc_array

if args.cmd == 'list':
    fmt_entry = '%-32s %s'
    desc_array = collect_test_desc_from_dir(args.dir)
    print(fmt_entry % ('Name', 'Description'))
    for desc in desc_array:
        print(fmt_entry % (desc['_test-name'], desc['description']))

def test_abort(test, message):
    print("\t%s: %s" % (test, message))
    print("=== Test %s ABORT ===" % (test))
    sys.exit(1)
    return False

def test_fail(test, message):
    print("\t%s: %s" % (test, message))
    print("=== Test %s FAIL ===" % (test))
    return False

def test_skip(test, message):
    print("\t%s: %s" % (test, message))
    print("=== Test %s SKIP ===" % (test))
    return True

def test_over(test):
    print("=== Test %s ERROR OVER ===" % (test))
    return True

def test_pass(test):
    print("=== Test %s PASS ===" % (test))
    return True

def test_updated(test):
    print("=== Test %s UPDATED ===" % (test))
    return True

def run_hexdump(path):
    p = subprocess.Popen([args.hexdump, "-C", path],
                         stdout = subprocess.PIPE,
                         close_fds = True)
    if p.wait() == 0:
        return p
    return None

def show_std(stdname, data):
    print("\t--- %s" % (stdname))
    for i in data.split("\n"):
        print("\t%s" % i)
    print("\t---")

def cmp_std(test, data_name, data, match):
    match_data = read_stdfile(match)
    if match_data == None:
        return test_fail(test, "Can't read " + match)
    if data != match_data:
        print("\t--- %s" % (data_name))
        for i in data.split("\n"):
            print("\t%s" % i)
        print("\t--- %s" % (match))
        for i in match_data.split("\n"):
            print("\t%s" % i)

        diff = difflib.unified_diff(data.split("\n"), match_data.split("\n"),
                                    fromfile = data_name, tofile = match)
        for i in diff:
            print("\t%s" % i.strip("\n"))
        print("\t---")
        return False
    return True

def show_diff(test, patha, pathb):
    pa = run_hexdump(patha)
    pb = run_hexdump(pathb)
    if pa == None or pb == None:
        return test_fail(test, "Can't create dumps")
    sa = pa.stdout.read().decode("utf-8").strip("\n")
    sb = pb.stdout.read().decode("utf-8").strip("\n")
    print("\t--- hexdump %s" % (patha))
    for i in sa.split("\n"):
        print("\t%s" % i)
    print("\t--- hexdump %s" % (pathb))
    for i in sb.split("\n"):
        print("\t%s" % i)
    pa.stdout.close()
    pb.stdout.close()

    diff = difflib.unified_diff(sa.split("\n"), sb.split("\n"),
                                fromfile = patha, tofile = pathb)
    for i in diff:
        print("\t%s" % i.strip("\n"))
    print("\t---")
    return True

def prepare_run_opts(desc):
    opts = []

    if 'format' in desc:
        opts += ['-f', desc['format']]
    if 'option' in desc:
        opts += desc['option'].split(" ")
    for t in desc['target']:
        if 'output' in t:
            if 'option' in t:
                opts += t['option'].split(" ") + [desc['_base-dir'] + os.sep + t['output']]
            else:
                opts += ['-o', desc['_base-dir'] + os.sep + t['output']]
        if 'stdout' in t or 'stderr' in t:
            if 'option' in t:
                opts += t['option'].split(" ")
    if 'source' in desc:
        opts += [desc['_base-dir'] + os.sep + desc['source']]
    return opts

def exec_nasm(desc):
    print("\tProcessing %s" % (desc['_test-name']))
    opts = [args.nasm] + prepare_run_opts(desc)

    print("\tExecuting %s" % (" ".join(opts)))
    pnasm = subprocess.Popen(opts,
                             stdout = subprocess.PIPE,
                             stderr = subprocess.PIPE,
                             close_fds = True)
    if pnasm == None:
        test_fail(desc['_test-name'], "Unable to execute test")
        return None
    wait_rc = pnasm.wait();

    stdout = pnasm.stdout.read().decode("utf-8").strip("\n")
    stderr = pnasm.stderr.read().decode("utf-8").strip("\n")
    pnasm.stdout.close()
    pnasm.stderr.close()

    if desc['_wait'] != wait_rc:
        if stdout != "":
            show_std("stdout", stdout)
        if stderr != "":
            show_std("stderr", stderr)
        test_fail(desc['_test-name'],
                  "Unexpected ret code: " + str(wait_rc))
        return None, None, None
    return pnasm, stdout, stderr

def test_run(desc):
    print("=== Running %s ===" % (desc['_test-name']))

    pnasm, stdout, stderr = exec_nasm(desc)
    if pnasm == None:
        return False

    for t in desc['target']:
        if 'output' in t:
            output = desc['_base-dir'] + os.sep + t['output']
            match = desc['_base-dir'] + os.sep + t['match']
            if desc['_wait'] == 1:
                continue
            print("\tComparing %s %s" % (output, match))
            if filecmp.cmp(match, output) == False:
                show_diff(desc['_test-name'], match, output)
                return test_fail(desc['_test-name'], match + " and " + output + " files are different")
        elif 'stdout' in t:
            print("\tComparing stdout")
            match = desc['_base-dir'] + os.sep + t['stdout']
            if cmp_std(desc['_test-name'], 'stdout', stdout, match) == False:
                return test_fail(desc['_test-name'], "Stdout mismatch")
            else:
                stdout = ""
        elif 'stderr' in t:
            print("\tComparing stderr")
            match = desc['_base-dir'] + os.sep + t['stderr']
            if cmp_std(desc['_test-name'], 'stderr', stderr, match) == False:
                return test_fail(desc['_test-name'], "Stderr mismatch")
            else:
                stderr = ""

    if stdout != "":
        show_std("stdout", stdout)
        return test_fail(desc['_test-name'], "Stdout is not empty")

    if stderr != "":
        show_std("stderr", stderr)
        return test_fail(desc['_test-name'], "Stderr is not empty")

    return test_pass(desc['_test-name'])

#
# Compile sources and generate new targets
def test_update(desc):
    print("=== Updating %s ===" % (desc['_test-name']))

    if 'update' in desc and desc['update'] == 'false':
        return test_skip(desc['_test-name'], "No output provided")

    pnasm, stdout, stderr = exec_nasm(desc)
    if pnasm == None:
        return False

    for t in desc['target']:
        if 'output' in t:
            output = desc['_base-dir'] + os.sep + t['output']
            match = desc['_base-dir'] + os.sep + t['match']
            print("\tMoving %s to %s" % (output, match))
            os.rename(output, match)
        if 'stdout' in t:
            match = desc['_base-dir'] + os.sep + t['stdout']
            print("\tMoving %s to %s" % ('stdout', match))
            with open(match, "wb") as f:
                f.write(stdout)
                f.close()
        if 'stderr' in t:
            match = desc['_base-dir'] + os.sep + t['stderr']
            print("\tMoving %s to %s" % ('stderr', match))
            with open(match, "wb") as f:
                f.write(stderr)
                f.close()

    return test_updated(desc['_test-name'])

if args.cmd == 'run':
    desc_array = []
    if args.test == None:
        desc_array = collect_test_desc_from_dir(args.dir)
    else:
        desc_array = collect_test_desc_from_file(args.test)
        if len(desc_array) == 0:
            test_abort(args.test, "Can't obtain test descriptors")

    for desc in desc_array:
        if test_run(desc) == False:
            if 'error' in desc and desc['error'] == 'over':
                test_over(desc['_test-name'])
            else:
                test_abort(desc['_test-name'], "Error detected")

if args.cmd == 'update':
    desc_array = []
    if args.test == None:
        desc_array = collect_test_desc_from_dir(args.dir)
    else:
        desc_array = collect_test_desc_from_file(args.test)
        if len(desc_array) == 0:
            test_abort(args.test, "Can't obtain a test descriptors")

    for desc in desc_array:
        if test_update(desc) == False:
            if 'error' in desc and desc['error'] == 'over':
                test_over(desc['_test-name'])
            else:
                test_abort(desc['_test-name'], "Error detected")
