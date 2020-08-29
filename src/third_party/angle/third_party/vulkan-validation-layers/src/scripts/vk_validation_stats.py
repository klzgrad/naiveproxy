#!/usr/bin/env python3
# Copyright (c) 2015-2020 The Khronos Group Inc.
# Copyright (c) 2015-2020 Valve Corporation
# Copyright (c) 2015-2020 LunarG, Inc.
# Copyright (c) 2015-2020 Google Inc.
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
# Author: Tobin Ehlis <tobine@google.com>
# Author: Dave Houlton <daveh@lunarg.com>
# Author: Shannon McPherson <shannon@lunarg.com>

import argparse
import common_codegen
import csv
import glob
import html
import json
import operator
import os
import platform
import re
import sys
import time
import unicodedata
from collections import defaultdict

verbose_mode = False
txt_db = False
csv_db = False
html_db = False
txt_filename = "validation_error_database.txt"
csv_filename = "validation_error_database.csv"
html_filename = "validation_error_database.html"
header_filename = "vk_validation_error_messages.h"
vuid_prefixes = ['VUID-', 'UNASSIGNED-', 'kVUID_']

# Hard-coded flags that could be command line args, if we decide that's useful
ignore_unassigned = True # These are not found in layer code unless they appear explicitly (most don't), so produce false positives

layer_source_files = [common_codegen.repo_relative(path) for path in [
    'layers/buffer_validation.cpp',
    'layers/core_validation.cpp',
    'layers/descriptor_sets.cpp',
    'layers/drawdispatch.cpp',
    'layers/parameter_validation_utils.cpp',
    'layers/object_tracker_utils.cpp',
    'layers/shader_validation.cpp',
    'layers/stateless_validation.h',
    'layers/generated/parameter_validation.cpp',
    'layers/generated/object_tracker.cpp',
]]

test_source_files = glob.glob(os.path.join(common_codegen.repo_relative('tests'), '*.cpp'))

unassigned_vuid_files = [common_codegen.repo_relative(path) for path in [
    'layers/stateless_validation.h',
    'layers/core_validation_error_enums.h',
    'layers/object_lifetime_validation.h'
]]

def printHelp():
    print ("Usage:")
    print ("  python vk_validation_stats.py <json_file>")
    print ("                                [ -c ]")
    print ("                                [ -todo ]")
    print ("                                [ -vuid <vuid_name> ]")
    print ("                                [ -text [ <text_out_filename>] ]")
    print ("                                [ -csv  [ <csv_out_filename>]  ]")
    print ("                                [ -html [ <html_out_filename>] ]")
    print ("                                [ -export_header ]")
    print ("                                [ -summary ]")
    print ("                                [ -verbose ]")
    print ("                                [ -help ]")
    print ("\n  The vk_validation_stats script parses validation layer source files to")
    print ("  determine the set of valid usage checks and tests currently implemented,")
    print ("  and generates coverage values by comparing against the full set of valid")
    print ("  usage identifiers in the Vulkan-Headers registry file 'validusage.json'")
    print ("\nArguments: ")
    print (" <json-file>       (required) registry file 'validusage.json'")
    print (" -c                report consistency warnings")
    print (" -todo             report unimplemented VUIDs")
    print (" -vuid <vuid_name> report status of individual VUID <vuid_name>")
    print (" -unassigned       report unassigned VUIDs")
    print (" -text [filename]  output the error database text to <text_database_filename>,")
    print ("                   defaults to 'validation_error_database.txt'")
    print (" -csv [filename]   output the error database in csv to <csv_database_filename>,")
    print ("                   defaults to 'validation_error_database.csv'")
    print (" -html [filename]  output the error database in html to <html_database_filename>,")
    print ("                   defaults to 'validation_error_database.html'")
    print (" -export_header    export a new VUID error text header file to <%s>" % header_filename)
    print (" -summary          output summary of VUID coverage")
    print (" -verbose          show your work (to stdout)")

class ValidationJSON:
    def __init__(self, filename):
        self.filename = filename
        self.explicit_vuids = set()
        self.implicit_vuids = set()
        self.all_vuids = set()
        self.vuid_db = defaultdict(list) # Maps VUID string to list of json-data dicts
        self.apiversion = ""
        self.duplicate_vuids = set()

        # A set of specific regular expression substitutions needed to clean up VUID text
        self.regex_dict = {}
        self.regex_dict[re.compile('<.*?>|&(amp;)+lt;|&(amp;)+gt;')] = ""
        self.regex_dict[re.compile(r'\\\(codeSize \\over 4\\\)')] = "(codeSize/4)"
        self.regex_dict[re.compile(r'\\\(\\lceil\{\\mathit\{rasterizationSamples} \\over 32}\\rceil\\\)')] = "(rasterizationSamples/32)"
        self.regex_dict[re.compile(r'\\\(\\left\\lceil{\\frac{maxFramebufferWidth}{minFragmentDensityTexelSize_{width}}}\\right\\rceil\\\)')] = "the ceiling of maxFramebufferWidth/minFragmentDensityTexelSize.width"
        self.regex_dict[re.compile(r'\\\(\\left\\lceil{\\frac{maxFramebufferHeight}{minFragmentDensityTexelSize_{height}}}\\right\\rceil\\\)')] = "the ceiling of maxFramebufferHeight/minFragmentDensityTexelSize.height"
        self.regex_dict[re.compile(r'\\\(\\left\\lceil{\\frac{width}{maxFragmentDensityTexelSize_{width}}}\\right\\rceil\\\)')] = "the ceiling of width/maxFragmentDensityTexelSize.width"
        self.regex_dict[re.compile(r'\\\(\\left\\lceil{\\frac{height}{maxFragmentDensityTexelSize_{height}}}\\right\\rceil\\\)')] = "the ceiling of height/maxFragmentDensityTexelSize.height"
        self.regex_dict[re.compile(r'\\\(\\textrm\{codeSize} \\over 4\\\)')] = "(codeSize/4)"

        # Regular expression for characters outside ascii range
        self.unicode_regex = re.compile('[^\x00-\x7f]')
        # Mapping from unicode char to ascii approximation
        self.unicode_dict = {
            '\u002b' : '+',  # PLUS SIGN
            '\u00b4' : "'",  # ACUTE ACCENT
            '\u200b' : '',   # ZERO WIDTH SPACE
            '\u2018' : "'",  # LEFT SINGLE QUOTATION MARK
            '\u2019' : "'",  # RIGHT SINGLE QUOTATION MARK
            '\u201c' : '"',  # LEFT DOUBLE QUOTATION MARK
            '\u201d' : '"',  # RIGHT DOUBLE QUOTATION MARK
            '\u2026' : '...',# HORIZONTAL ELLIPSIS
            '\u2032' : "'",  # PRIME
            '\u2192' : '->', # RIGHTWARDS ARROW
        }

    def sanitize(self, text, location):
        # Strip leading/trailing whitespace
        text = text.strip()
        # Apply regex text substitutions
        for regex, replacement in self.regex_dict.items():
            text = re.sub(regex, replacement, text)
        # Un-escape html entity codes, ie &#XXXX;
        text = html.unescape(text)
        # Apply unicode substitutions
        for unicode in self.unicode_regex.findall(text):
            try:
                # Replace known chars
                text = text.replace(unicode, self.unicode_dict[unicode])
            except KeyError:
                # Strip and warn on unrecognized chars
                text = text.replace(unicode, '')
                name = unicodedata.name(unicode, 'UNKNOWN')
                print('Warning: Unknown unicode character \\u{:04x} ({}) at {}'.format(ord(unicode), name, location))
        return text

    def read(self):
        self.json_dict = {}
        if os.path.isfile(self.filename):
            json_file = open(self.filename, 'r', encoding='utf-8')
            self.json_dict = json.load(json_file)
            json_file.close()
        if len(self.json_dict) == 0:
            print("Error: Error loading validusage.json file <%s>" % self.filename)
            sys.exit(-1)
        try:
            version = self.json_dict['version info']
            validation = self.json_dict['validation']
            self.apiversion = version['api version']
        except:
            print("Error: Failure parsing validusage.json object")
            sys.exit(-1)

        # Parse vuid from json into local databases
        for apiname in validation.keys():
            apidict = validation[apiname]
            for ext in apidict.keys():
                vlist = apidict[ext]
                for ventry in vlist:
                    vuid_string = ventry['vuid']
                    if (vuid_string[-5:-1].isdecimal()):
                        self.explicit_vuids.add(vuid_string)    # explicit end in 5 numeric chars
                        vtype = 'explicit'
                    else:
                        self.implicit_vuids.add(vuid_string)    # otherwise, implicit
                        vtype = 'implicit'
                    vuid_text = self.sanitize(ventry['text'], vuid_string)
                    self.vuid_db[vuid_string].append({'api':apiname, 'ext':ext, 'type':vtype, 'text':vuid_text})
        self.all_vuids = self.explicit_vuids | self.implicit_vuids
        self.duplicate_vuids = set({v for v in self.vuid_db if len(self.vuid_db[v]) > 1})
        if len(self.duplicate_vuids) > 0:
            print("Warning: duplicate VUIDs found in validusage.json")


def buildKvuidDict():
    kvuid_dict = {}

    for uf in unassigned_vuid_files:
        line_num = 0
        with open(uf) as f:
            for line in f:
                line_num = line_num + 1
                if True in [line.strip().startswith(comment) for comment in ['//', '/*']]:
                    continue

                if 'kVUID_' in line:
                    kvuid_pos = line.find('kVUID_'); assert(kvuid_pos >= 0)
                    eq_pos = line.find('=', kvuid_pos)
                    if eq_pos >= 0:
                        kvuid = line[kvuid_pos:eq_pos].strip(' \t\n;"')
                        unassigned_str = line[eq_pos+1:].strip(' \t\n;"')
                        kvuid_dict[kvuid] = unassigned_str
    return kvuid_dict

class ValidationSource:
    def __init__(self, source_file_list):
        self.source_files = source_file_list
        self.vuid_count_dict = {} # dict of vuid values to the count of how much they're used, and location of where they're used
        self.duplicated_checks = 0
        self.explicit_vuids = set()
        self.implicit_vuids = set()
        self.unassigned_vuids = set()
        self.all_vuids = set()

    def parse(self):
        kvuid_dict = buildKvuidDict()

        # build self.vuid_count_dict
        prepend = None
        for sf in self.source_files:
            line_num = 0
            with open(sf, encoding='utf-8') as f:
                for line in f:
                    line_num = line_num + 1
                    if True in [line.strip().startswith(comment) for comment in ['//', '/*']]:
                        if 'VUID-' not in line or 'TODO:' in line:
                            continue
                    # Find vuid strings
                    if prepend is not None:
                        line = prepend[:-2] + line.lstrip().lstrip('"') # join lines skipping CR, whitespace and trailing/leading quote char
                        prepend = None
                    if any(prefix in line for prefix in vuid_prefixes):
                        # Replace the '(' of lines containing validation helper functions with ' ' to make them easier to parse
                        line = line.replace("(", " ")
                        line_list = line.split()

                        # A VUID string that has been broken by clang will start with a vuid prefix and end with -, and will be last in the list
                        broken_vuid = line_list[-1].strip('"')
                        if any(broken_vuid.startswith(prefix) for prefix in vuid_prefixes) and broken_vuid.endswith('-'):
                            prepend = line
                            continue

                        vuid_list = []
                        for str in line_list:
                            if any(prefix in str for prefix in vuid_prefixes):
                                vuid_list.append(str.strip(',);{}"*'))
                        for vuid in vuid_list:
                            if vuid.startswith('kVUID_'): vuid = kvuid_dict[vuid]
                            if vuid not in self.vuid_count_dict:
                                self.vuid_count_dict[vuid] = {}
                                self.vuid_count_dict[vuid]['count'] = 1
                                self.vuid_count_dict[vuid]['file_line'] = []
                            else:
                                if self.vuid_count_dict[vuid]['count'] == 1:    # only count first time duplicated
                                    self.duplicated_checks = self.duplicated_checks + 1
                                self.vuid_count_dict[vuid]['count'] = self.vuid_count_dict[vuid]['count'] + 1
                            self.vuid_count_dict[vuid]['file_line'].append('%s,%d' % (sf, line_num))
        # Sort vuids by type
        for vuid in self.vuid_count_dict.keys():
            if (vuid.startswith('VUID-')):
                if (vuid[-5:-1].isdecimal()):
                    self.explicit_vuids.add(vuid)    # explicit end in 5 numeric chars
                else:
                    self.implicit_vuids.add(vuid)
            elif (vuid.startswith('UNASSIGNED-')):
                self.unassigned_vuids.add(vuid)
            else:
                print("Unable to categorize VUID: %s" % vuid)
                print("Confused while parsing VUIDs in layer source code - cannot proceed. (FIXME)")
                exit(-1)
        self.all_vuids = self.explicit_vuids | self.implicit_vuids | self.unassigned_vuids

# Class to parse the validation layer test source and store testnames
class ValidationTests:
    def __init__(self, test_file_list, test_group_name=['VkLayerTest', 'VkPositiveLayerTest', 'VkWsiEnabledLayerTest', 'VkBestPracticesLayerTest']):
        self.test_files = test_file_list
        self.test_trigger_txt_list = []
        for tg in test_group_name:
            self.test_trigger_txt_list.append('TEST_F(%s' % tg)
        self.explicit_vuids = set()
        self.implicit_vuids = set()
        self.unassigned_vuids = set()
        self.all_vuids = set()
        #self.test_to_vuids = {} # Map test name to VUIDs tested
        self.vuid_to_tests = defaultdict(set) # Map VUIDs to set of test names where implemented

    # Parse test files into internal data struct
    def parse(self):
        kvuid_dict = buildKvuidDict()

        # For each test file, parse test names into set
        grab_next_line = False # handle testname on separate line than wildcard
        testname = ''
        prepend = None
        for test_file in self.test_files:
            with open(test_file) as tf:
                for line in tf:
                    if True in [line.strip().startswith(comment) for comment in ['//', '/*']]:
                        continue

                    # if line ends in a broken VUID string, fix that before proceeding
                    if prepend is not None:
                        line = prepend[:-2] + line.lstrip().lstrip('"') # join lines skipping CR, whitespace and trailing/leading quote char
                        prepend = None
                    if any(prefix in line for prefix in vuid_prefixes):
                        line_list = line.split()

                        # A VUID string that has been broken by clang will start with a vuid prefix and end with -, and will be last in the list
                        broken_vuid = line_list[-1].strip('"')
                        if any(broken_vuid.startswith(prefix) for prefix in vuid_prefixes) and broken_vuid.endswith('-'):
                            prepend = line
                            continue

                    if any(ttt in line for ttt in self.test_trigger_txt_list):
                        testname = line.split(',')[-1]
                        testname = testname.strip().strip(' {)')
                        if ('' == testname):
                            grab_next_line = True
                            continue
                        #self.test_to_vuids[testname] = []
                    if grab_next_line: # test name on its own line
                        grab_next_line = False
                        testname = testname.strip().strip(' {)')
                        #self.test_to_vuids[testname] = []
                    if any(prefix in line for prefix in vuid_prefixes):
                        line_list = re.split('[\s{}[\]()"]+',line)
                        for sub_str in line_list:
                            if any(prefix in sub_str for prefix in vuid_prefixes):
                                vuid_str = sub_str.strip(',);:"*')
                                if vuid_str.startswith('kVUID_'): vuid_str = kvuid_dict[vuid_str]
                                self.vuid_to_tests[vuid_str].add(testname)
                                #self.test_to_vuids[testname].append(vuid_str)
                                if (vuid_str.startswith('VUID-')):
                                    if (vuid_str[-5:-1].isdecimal()):
                                        self.explicit_vuids.add(vuid_str)    # explicit end in 5 numeric chars
                                    else:
                                        self.implicit_vuids.add(vuid_str)
                                elif (vuid_str.startswith('UNASSIGNED-')):
                                    self.unassigned_vuids.add(vuid_str)
                                else:
                                    print("Unable to categorize VUID: %s" % vuid_str)
                                    print("Confused while parsing VUIDs in test code - cannot proceed. (FIXME)")
                                    exit(-1)
        self.all_vuids = self.explicit_vuids | self.implicit_vuids | self.unassigned_vuids

# Class to do consistency checking
#
class Consistency:
    def __init__(self, all_json, all_checks, all_tests):
        self.valid = all_json
        self.checks = all_checks
        self.tests = all_tests

    # Report undefined VUIDs in source code
    def undef_vuids_in_layer_code(self):
        undef_set = self.checks - self.valid
        undef_set.discard('VUID-Undefined') # don't report Undefined
        if ignore_unassigned:
            unassigned = set({uv for uv in undef_set if uv.startswith('UNASSIGNED-')})
            undef_set = undef_set - unassigned
        if (len(undef_set) > 0):
            print("\nFollowing VUIDs found in layer code are not defined in validusage.json (%d):" % len(undef_set))
            undef = list(undef_set)
            undef.sort()
            for vuid in undef:
                print("    %s" % vuid)
            return False
        return True

    # Report undefined VUIDs in tests
    def undef_vuids_in_tests(self):
        undef_set = self.tests - self.valid
        undef_set.discard('VUID-Undefined') # don't report Undefined
        if ignore_unassigned:
            unassigned = set({uv for uv in undef_set if uv.startswith('UNASSIGNED-')})
            undef_set = undef_set - unassigned
        if (len(undef_set) > 0):
            ok = False
            print("\nFollowing VUIDs found in layer tests are not defined in validusage.json (%d):" % len(undef_set))
            undef = list(undef_set)
            undef.sort()
            for vuid in undef:
                print("    %s" % vuid)
            return False
        return True

    # Report vuids in tests that are not in source
    def vuids_tested_not_checked(self):
        undef_set = self.tests - self.checks
        undef_set.discard('VUID-Undefined') # don't report Undefined
        if ignore_unassigned:
            unassigned = set()
            for vuid in undef_set:
                if vuid.startswith('UNASSIGNED-'):
                    unassigned.add(vuid)
            undef_set = undef_set - unassigned
        if (len(undef_set) > 0):
            ok = False
            print("\nFollowing VUIDs found in tests but are not checked in layer code (%d):" % len(undef_set))
            undef = list(undef_set)
            undef.sort()
            for vuid in undef:
                print("    %s" % vuid)
            return False
        return True

    # TODO: Explicit checked VUIDs which have no test
    # def explicit_vuids_checked_not_tested(self):


# Class to output database in various flavors
#
class OutputDatabase:
    def __init__(self, val_json, val_source, val_tests):
        self.vj = val_json
        self.vs = val_source
        self.vt = val_tests
        self.header_version = "/* THIS FILE IS GENERATED - DO NOT EDIT (scripts/vk_validation_stats.py) */"
        self.header_version += "\n/* Vulkan specification version: %s */" % val_json.apiversion
        self.header_preamble = """
/*
 * Vulkan
 *
 * Copyright (c) 2016-2020 Google Inc.
 * Copyright (c) 2016-2020 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Tobin Ehlis <tobine@google.com>
 * Author: Dave Houlton <daveh@lunarg.com>
 */

#pragma once

// Disable auto-formatting for generated file
// clang-format off

// Mapping from VUID string to the corresponding spec text
typedef struct _vuid_spec_text_pair {
    const char * vuid;
    const char * spec_text;
    const char * url_id;
} vuid_spec_text_pair;

static const vuid_spec_text_pair vuid_spec_text[] = {
"""
        self.header_postamble = """};
"""
    def dump_txt(self, only_unimplemented = False):
        print("\n Dumping database to text file: %s" % txt_filename)
        with open (txt_filename, 'w') as txt:
            txt.write("## VUID Database\n")
            txt.write("## Format: VUID_NAME | CHECKED | TEST | TYPE | API/STRUCT | EXTENSION | VUID_TEXT\n##\n")
            vuid_list = list(self.vj.all_vuids)
            vuid_list.sort()
            for vuid in vuid_list:
                db_list = self.vj.vuid_db[vuid]
                db_list.sort(key=operator.itemgetter('ext')) # sort list to ease diffs of output file
                for db_entry in db_list:
                    checked = 'N'
                    if vuid in self.vs.all_vuids:
                        if only_unimplemented:
                            continue
                        else:
                            checked = 'Y'
                    test = 'None'
                    if vuid in self.vt.vuid_to_tests:
                        test_list = list(self.vt.vuid_to_tests[vuid])
                        test_list.sort()   # sort tests, for diff-ability
                        sep = ', '
                        test = sep.join(test_list)

                    txt.write("%s | %s | %s | %s | %s | %s | %s\n" % (vuid, checked, test, db_entry['type'], db_entry['api'], db_entry['ext'], db_entry['text']))

    def dump_csv(self, only_unimplemented = False):
        print("\n Dumping database to csv file: %s" % csv_filename)
        with open (csv_filename, 'w', newline='') as csvfile:
            cw = csv.writer(csvfile)
            cw.writerow(['VUID_NAME','CHECKED','TEST','TYPE','API/STRUCT','EXTENSION','VUID_TEXT'])
            vuid_list = list(self.vj.all_vuids)
            vuid_list.sort()
            for vuid in vuid_list:
                for db_entry in self.vj.vuid_db[vuid]:
                    row = [vuid]
                    if vuid in self.vs.all_vuids:
                        if only_unimplemented:
                            continue
                        else:
                            row.append('Y')
                    else:
                        row.append('N')
                    test = 'None'
                    if vuid in self.vt.vuid_to_tests:
                        sep = ', '
                        test = sep.join(self.vt.vuid_to_tests[vuid])
                    row.append(test)
                    row.append(db_entry['type'])
                    row.append(db_entry['api'])
                    row.append(db_entry['ext'])
                    row.append(db_entry['text'])
                    cw.writerow(row)

    def dump_html(self, only_unimplemented = False):
        print("\n Dumping database to html file: %s" % html_filename)
        preamble = '<!DOCTYPE html>\n<html>\n<head>\n<style>\ntable, th, td {\n border: 1px solid black;\n border-collapse: collapse; \n}\n</style>\n<body>\n<h2>Valid Usage Database</h2>\n<font size="2" face="Arial">\n<table style="width:100%">\n'
        headers = '<tr><th>VUID NAME</th><th>CHECKED</th><th>TEST</th><th>TYPE</th><th>API/STRUCT</th><th>EXTENSION</th><th>VUID TEXT</th></tr>\n'
        with open (html_filename, 'w') as hfile:
            hfile.write(preamble)
            hfile.write(headers)
            vuid_list = list(self.vj.all_vuids)
            vuid_list.sort()
            for vuid in vuid_list:
                for db_entry in self.vj.vuid_db[vuid]:
                    checked = '<span style="color:red;">N</span>'
                    if vuid in self.vs.all_vuids:
                        if only_unimplemented:
                            continue
                        else:
                            checked = '<span style="color:limegreen;">Y</span>'
                    hfile.write('<tr><th>%s</th>' % vuid)
                    hfile.write('<th>%s</th>' % checked)
                    test = 'None'
                    if vuid in self.vt.vuid_to_tests:
                        sep = ', '
                        test = sep.join(self.vt.vuid_to_tests[vuid])
                    hfile.write('<th>%s</th>' % test)
                    hfile.write('<th>%s</th>' % db_entry['type'])
                    hfile.write('<th>%s</th>' % db_entry['api'])
                    hfile.write('<th>%s</th>' % db_entry['ext'])
                    hfile.write('<th>%s</th></tr>\n' % db_entry['text'])
            hfile.write('</table>\n</body>\n</html>\n')

    # make list of spec versions containing given VUID
    @staticmethod
    def make_vuid_spec_version_list(pattern, max_minor_version):
        assert pattern

        all_editions_list = []
        for e in reversed(range(max_minor_version+1)):
            all_editions_list.append({"version": e, "ext": True,  "khr" : False})
            all_editions_list.append({"version": e, "ext": False, "khr" : True})
            all_editions_list.append({"version": e, "ext": False, "khr" : False})

        if pattern == 'core':
            return all_editions_list

        # pattern is series of parentheses separated by plus
        # each parentheses can be prepended by negation (!)
        # each parentheses contains list of extensions or vk versions separated by either comma or plus
        edition_list_out = []
        for edition in all_editions_list:
            resolved_pattern = True

            raw_terms = re.split(r'\)\+', pattern)
            for raw_term in raw_terms:
                negated = raw_term.startswith('!')
                term = raw_term.lstrip('!(').rstrip(')')
                conjunction = '+' in term
                disjunction = ',' in term
                assert not (conjunction and disjunction)
                if conjunction: features = term.split('+')
                elif disjunction: features = term.split(',')
                else: features = [term]
                assert features

                def isDefined(feature, edition):
                    def getVersion(f): return int(f.replace('VK_VERSION_1_', '', 1))
                    def isVersion(f): return f.startswith('VK_VERSION_') and feature != 'VK_VERSION_1_0' and getVersion(feature) < 1024
                    def isExtension(f): return f.startswith('VK_') and not isVersion(f)
                    def isKhr(f): return f.startswith('VK_KHR_')

                    assert isExtension(feature) or isVersion(feature)

                    if isVersion(feature) and getVersion(feature) <= edition['version']: return True
                    elif isExtension(feature) and edition['ext']: return True
                    elif isKhr(feature) and edition['khr']: return True
                    else: return False

                if not negated and (conjunction or (not conjunction and not disjunction)): # all defined
                    resolved_term = True
                    for feature in features:
                        if not isDefined(feature, edition): resolved_term = False
                elif negated and conjunction: # at least one not defined
                    resolved_term = False
                    for feature in features:
                        if not isDefined(feature, edition): resolved_term = True
                elif not negated and disjunction: # at least one defined
                    resolved_term = False
                    for feature in features:
                        if isDefined(feature, edition): resolved_term = True
                elif negated and (disjunction or (not conjunction and not disjunction)): # none defined
                    resolved_term = True
                    for feature in features:
                        if isDefined(feature, edition): resolved_term = False

                resolved_pattern = resolved_pattern and resolved_term
            if resolved_pattern: edition_list_out.append(edition)
        return edition_list_out


    def export_header(self):
        if verbose_mode:
            print("\n Exporting header file to: %s" % header_filename)
        with open (header_filename, 'w', newline='\n') as hfile:
            hfile.write(self.header_version)
            hfile.write(self.header_preamble)
            vuid_list = list(self.vj.all_vuids)
            vuid_list.sort()
            cmd_dict = {}
            minor_version = int(self.vj.apiversion.split('.')[1])

            for vuid in vuid_list:
                db_entry = self.vj.vuid_db[vuid][0]

                spec_list = self.make_vuid_spec_version_list(db_entry['ext'], minor_version)

                if not spec_list: spec_url_id = 'default'
                elif spec_list[0]['ext']: spec_url_id = '1.%s-extensions' % spec_list[0]['version']
                elif spec_list[0]['khr']: spec_url_id = '1.%s-khr-extensions' % spec_list[0]['version']
                else: spec_url_id = '1.%s' % spec_list[0]['version']

                # Escape quotes when generating C strings for source code
                db_text = db_entry['text'].replace('"', '\\"')
                hfile.write('    {"%s", "%s", "%s"},\n' % (vuid, db_text, spec_url_id))
                # For multiply-defined VUIDs, include versions with extension appended
                if len(self.vj.vuid_db[vuid]) > 1:
                    print('Error: Found a duplicate VUID: %s' % vuid)
                    sys.exit(-1)
                if 'commandBuffer must be in the recording state' in db_text:
                    cmd_dict[vuid] = db_text
            hfile.write(self.header_postamble)

            # Generate the information for validating recording state VUID's
            cmd_prefix = 'prefix##'
            cmd_regex = re.compile(r'VUID-vk(Cmd|End)(\w+)')
            cmd_vuid_vector = ['    "VUID_Undefined"']
            cmd_name_vector = [ '    "Command_Undefined"' ]
            cmd_enum = ['    ' + cmd_prefix + 'NONE = 0']

            cmd_ordinal = 1
            for vuid, db_text in sorted(cmd_dict.items()):
                cmd_match = cmd_regex.match(vuid)
                if cmd_match.group(1) == "End":
                    end = "END"
                else:
                    end = ""
                cmd_name_vector.append('    "vk'+ cmd_match.group(1) + cmd_match.group(2) + '"')
                cmd_name = cmd_prefix + end + cmd_match.group(2).upper()
                cmd_enum.append('    {} = {}'.format(cmd_name, cmd_ordinal))
                cmd_ordinal += 1
                cmd_vuid_vector.append('    "{}"'.format(vuid))

            hfile.write('\n// Defines to allow creating "must be recording" meta data\n')
            cmd_enum.append('    {}RANGE_SIZE = {}'.format(cmd_prefix, cmd_ordinal))
            cmd_enum_string = '#define VUID_CMD_ENUM_LIST(prefix)\\\n' + ',\\\n'.join(cmd_enum) + '\n\n'
            hfile.write(cmd_enum_string)
            cmd_name_list_string = '#define VUID_CMD_NAME_LIST\\\n' + ',\\\n'.join(cmd_name_vector) + '\n\n'
            hfile.write(cmd_name_list_string)
            vuid_vector_string = '#define VUID_MUST_BE_RECORDING_LIST\\\n' + ',\\\n'.join(cmd_vuid_vector) + '\n'
            hfile.write(vuid_vector_string)

def main(argv):
    global verbose_mode
    global txt_filename
    global csv_filename
    global html_filename

    run_consistency = False
    report_unimplemented = False
    report_unassigned = False
    get_vuid_status = ''
    txt_out = False
    csv_out = False
    html_out = False
    header_out = False
    show_summary = False

    if (1 > len(argv)):
        printHelp()
        sys.exit()

    # Parse script args
    json_filename = argv[0]
    i = 1
    while (i < len(argv)):
        arg = argv[i]
        i = i + 1
        if (arg == '-c'):
            run_consistency = True
        elif (arg == '-vuid'):
            get_vuid_status = argv[i]
            i = i + 1
        elif (arg == '-todo'):
            report_unimplemented = True
        elif (arg == '-unassigned'):
            report_unassigned = True
        elif (arg == '-text'):
            txt_out = True
            # Set filename if supplied, else use default
            if i < len(argv) and not argv[i].startswith('-'):
                txt_filename = argv[i]
                i = i + 1
        elif (arg == '-csv'):
            csv_out = True
            # Set filename if supplied, else use default
            if i < len(argv) and not argv[i].startswith('-'):
                csv_filename = argv[i]
                i = i + 1
        elif (arg == '-html'):
            html_out = True
            # Set filename if supplied, else use default
            if i < len(argv) and not argv[i].startswith('-'):
                html_filename = argv[i]
                i = i + 1
        elif (arg == '-export_header'):
            header_out = True
        elif (arg in ['-verbose']):
            verbose_mode = True
        elif (arg in ['-summary']):
            show_summary = True
        elif (arg in ['-help', '-h']):
            printHelp()
            sys.exit()
        else:
            print("Unrecognized argument: %s\n" % arg)
            printHelp()
            sys.exit()

    result = 0 # Non-zero result indicates an error case

    # Parse validusage json
    val_json = ValidationJSON(json_filename)
    val_json.read()
    exp_json = len(val_json.explicit_vuids)
    imp_json = len(val_json.implicit_vuids)
    all_json = len(val_json.all_vuids)
    if verbose_mode:
        print("Found %d unique error vuids in validusage.json file." % all_json)
        print("  %d explicit" % exp_json)
        print("  %d implicit" % imp_json)
        if len(val_json.duplicate_vuids) > 0:
            print("%d VUIDs appear in validusage.json more than once." % len(val_json.duplicate_vuids))
            for vuid in val_json.duplicate_vuids:
                print("  %s" % vuid)
                for ext in val_json.vuid_db[vuid]:
                    print("    with extension: %s" % ext['ext'])

    # Parse layer source files
    val_source = ValidationSource(layer_source_files)
    val_source.parse()
    exp_checks = len(val_source.explicit_vuids)
    imp_checks = len(val_source.implicit_vuids)
    all_checks = len(val_source.vuid_count_dict.keys())
    if verbose_mode:
        print("Found %d unique vuid checks in layer source code." % all_checks)
        print("  %d explicit" % exp_checks)
        print("  %d implicit" % imp_checks)
        print("  %d unassigned" % len(val_source.unassigned_vuids))
        print("  %d checks are implemented more that once" % val_source.duplicated_checks)

    # Parse test files
    val_tests = ValidationTests(test_source_files)
    val_tests.parse()
    exp_tests = len(val_tests.explicit_vuids)
    imp_tests = len(val_tests.implicit_vuids)
    all_tests = len(val_tests.all_vuids)
    if verbose_mode:
        print("Found %d unique error vuids in test source code." % all_tests)
        print("  %d explicit" % exp_tests)
        print("  %d implicit" % imp_tests)
        print("  %d unassigned" % len(val_tests.unassigned_vuids))

    # Process stats
    if show_summary:
        print("\nValidation Statistics (using validusage.json version %s)" % val_json.apiversion)
        print("  VUIDs defined in JSON file:  %04d explicit, %04d implicit, %04d total." % (exp_json, imp_json, all_json))
        print("  VUIDs checked in layer code: %04d explicit, %04d implicit, %04d total." % (exp_checks, imp_checks, all_checks))
        print("  VUIDs tested in layer tests: %04d explicit, %04d implicit, %04d total." % (exp_tests, imp_tests, all_tests))

        print("\nVUID check coverage")
        print("  Explicit VUIDs checked: %.1f%% (%d checked vs %d defined)" % ((100.0 * exp_checks / exp_json), exp_checks, exp_json))
        print("  Implicit VUIDs checked: %.1f%% (%d checked vs %d defined)" % ((100.0 * imp_checks / imp_json), imp_checks, imp_json))
        print("  Overall VUIDs checked:  %.1f%% (%d checked vs %d defined)" % ((100.0 * all_checks / all_json), all_checks, all_json))

        print("\nVUID test coverage")
        print("  Explicit VUIDs tested: %.1f%% (%d tested vs %d checks)" % ((100.0 * exp_tests / exp_checks), exp_tests, exp_checks))
        print("  Implicit VUIDs tested: %.1f%% (%d tested vs %d checks)" % ((100.0 * imp_tests / imp_checks), imp_tests, imp_checks))
        print("  Overall VUIDs tested:  %.1f%% (%d tested vs %d checks)" % ((100.0 * all_tests / all_checks), all_tests, all_checks))

    # Report status of a single VUID
    if len(get_vuid_status) > 1:
        print("\n\nChecking status of <%s>" % get_vuid_status);
        if get_vuid_status not in val_json.all_vuids and not get_vuid_status.startswith('UNASSIGNED-'):
            print('  Not a valid VUID string.')
        else:
            if get_vuid_status in val_source.explicit_vuids:
                print('  Implemented!')
                line_list = val_source.vuid_count_dict[get_vuid_status]['file_line']
                for line in line_list:
                    print('    => %s' % line)
            elif get_vuid_status in val_source.implicit_vuids:
                print('  Implemented! (Implicit)')
                line_list = val_source.vuid_count_dict[get_vuid_status]['file_line']
                for line in line_list:
                    print('    => %s' % line)
            else:
                print('  Not implemented.')
            if get_vuid_status in val_tests.all_vuids:
                print('  Has a test!')
                test_list = val_tests.vuid_to_tests[get_vuid_status]
                for test in test_list:
                    print('    => %s' % test)
            else:
                print('  Not tested.')

    # Report unimplemented explicit VUIDs
    if report_unimplemented:
        unim_explicit = val_json.explicit_vuids - val_source.explicit_vuids
        print("\n\n%d explicit VUID checks remain unimplemented:" % len(unim_explicit))
        ulist = list(unim_explicit)
        ulist.sort()
        for vuid in ulist:
            print("  => %s" % vuid)

    # Report unassigned VUIDs
    if report_unassigned:
        # TODO: I do not really want VUIDs created for warnings though here
        print("\n\n%d checks without a spec VUID:" % len(val_source.unassigned_vuids))
        ulist = list(val_source.unassigned_vuids)
        ulist.sort()
        for vuid in ulist:
            print("  => %s" % vuid)
            line_list = val_source.vuid_count_dict[vuid]['file_line']
            for line in line_list:
                print('    => %s' % line)
        print("\n%d tests without a spec VUID:" % len(val_source.unassigned_vuids))
        ulist = list(val_tests.unassigned_vuids)
        ulist.sort()
        for vuid in ulist:
            print("  => %s" % vuid)
            test_list = val_tests.vuid_to_tests[vuid]
            for test in test_list:
                print('    => %s' % test)

    # Consistency tests
    if run_consistency:
        print("\n\nRunning consistency tests...")
        con = Consistency(val_json.all_vuids, val_source.all_vuids, val_tests.all_vuids)
        ok = con.undef_vuids_in_layer_code()
        ok &= con.undef_vuids_in_tests()
        ok &= con.vuids_tested_not_checked()

        if ok:
            print("  OK! No inconsistencies found.")

    # Output database in requested format(s)
    db_out = OutputDatabase(val_json, val_source, val_tests)
    if txt_out:
        db_out.dump_txt(report_unimplemented)
    if csv_out:
        db_out.dump_csv(report_unimplemented)
    if html_out:
        db_out.dump_html(report_unimplemented)
    if header_out:
        db_out.export_header()
    return result

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
