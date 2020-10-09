#
# Copyright 2020 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
"""
Script testing capture_replay with angle_end2end_tests
"""

# Automation script will:
# 1. Build all tests in angle_end2end with frame capture enabled
# 2. Run each test with frame capture
# 3. Build CaptureReplayTest with cpp trace files
# 4. Run CaptureReplayTest
# 5. Output the number of test successes and failures. A test succeeds if no error occurs during
# its capture and replay, and the GL states at the end of two runs match. Any unexpected failure
# will return non-zero exit code

# Run this script with Python to test capture replay on angle_end2end tests
# python path/to/capture_replay_tests.py
# Command line arguments:
# --capture-build-dir: specifies capture build directory relative to angle folder.
# Default is out/CaptureDebug
# --replay-build-dir: specifies replay build directory relative to angle folder.
# Default is out/ReplayDebug
# --use-goma: uses goma for compiling and linking test. Off by default
# --gtest_filter: same as gtest_filter of Google's test framework. Default is */ES2_Vulkan
# --test-suite: test suite to execute on. Default is angle_end2end_tests
# --batch-count: number of tests in a batch. Default is 8
# --keep-temp-files: whether to keep the temp files and folders. Off by default
# --goma-dir: set goma directory. Default is the system's default
# --output-to-file: whether to write output to a result file. Off by default
# --result-file: name of the result file in the capture_replay_tests folder.
# Default is results.txt
# --verbose: Off by default

import argparse
import distutils.util
import fnmatch
import math
import multiprocessing
import os
import queue
import re
import shlex
import shutil
import subprocess
import sys
import time

from sys import platform

DEFAULT_CAPTURE_BUILD_DIR = "out/CaptureTest"  # relative to angle folder
DEFAULT_REPLAY_BUILD_DIR = "out/ReplayTest"  # relative to angle folder
DEFAULT_FILTER = "*/ES2_Vulkan"
DEFAULT_TEST_SUITE = "angle_end2end_tests"
REPLAY_SAMPLE_FOLDER = "src/tests/capture_replay_tests"  # relative to angle folder
DEFAULT_BATCH_COUNT = 8  # number of tests batched together
TRACE_FILE_SUFFIX = "_capture_context"  # because we only deal with 1 context right now
RESULT_TAG = "*RESULT"
TIME_BETWEEN_MESSAGE = 20  # in seconds
SUBPROCESS_TIMEOUT = 600  # in seconds
DEFAULT_RESULT_FILE = "results.txt"

switch_case_without_return_template = \
"""        case {case}:
            {namespace}::{call}({params});
            break;
"""

switch_case_with_return_template = \
"""        case {case}:
            return {namespace}::{call}({params});
"""

default_case_without_return_template = \
"""        default:
            break;"""
default_case_with_return_template = \
"""        default:
            return {default_val};"""

switch_statement_template = \
"""switch(test)
    {{
{switch_cases}{default_case}
    }}
"""

test_trace_info_init_template = \
"""    {{
        "{namespace}",
        {namespace}::kReplayFrameStart,
        {namespace}::kReplayFrameEnd,
        {namespace}::kReplayDrawSurfaceWidth,
        {namespace}::kReplayDrawSurfaceHeight,
        {namespace}::kDefaultFramebufferRedBits,
        {namespace}::kDefaultFramebufferGreenBits,
        {namespace}::kDefaultFramebufferBlueBits,
        {namespace}::kDefaultFramebufferAlphaBits,
        {namespace}::kDefaultFramebufferDepthBits,
        {namespace}::kDefaultFramebufferStencilBits,
        {namespace}::kIsBinaryDataCompressed
    }},
"""

composite_h_file_template = \
"""#pragma once
#include <vector>
#include <string>

{trace_headers}

using DecompressCallback = uint8_t *(*)(const std::vector<uint8_t> &);

void SetupContextReplay(uint32_t test);
void ReplayContextFrame(uint32_t test, uint32_t frameIndex);
void ResetContextReplay(uint32_t test);
std::vector<uint8_t> GetSerializedContextStateData(uint32_t test, uint32_t frameIndex);
void SetBinaryDataDecompressCallback(uint32_t test, DecompressCallback callback);
void SetBinaryDataDir(uint32_t test, const char *dataDir);

struct TestTraceInfo {{
    std::string testName;
    uint32_t replayFrameStart;
    uint32_t replayFrameEnd;
    EGLint replayDrawSurfaceWidth;
    EGLint replayDrawSurfaceHeight;
    EGLint defaultFramebufferRedBits;
    EGLint defaultFramebufferGreenBits;
    EGLint defaultFramebufferBlueBits;
    EGLint defaultFramebufferAlphaBits;
    EGLint defaultFramebufferDepthBits;
    EGLint defaultFramebufferStencilBits;
    bool isBinaryDataCompressed;
}};

extern std::vector<TestTraceInfo> testTraceInfos;
"""

composite_cpp_file_template = \
"""#include "{h_filename}"

std::vector<TestTraceInfo> testTraceInfos =
{{
{test_trace_info_inits}
}};
void SetupContextReplay(uint32_t test)
{{
    {setup_context1_replay_switch_statement}
}}

void ReplayContextFrame(uint32_t test, uint32_t frameIndex)
{{
    {replay_context1_replay_switch_statement}
}}

void ResetContextReplay(uint32_t test)
{{
    {reset_context1_replay_switch_statement}
}}

std::vector<uint8_t> GetSerializedContextStateData(uint32_t test, uint32_t frameIndex)
{{
    {get_serialized_context1_state_data_switch_statement}
}}

void SetBinaryDataDecompressCallback(uint32_t test, DecompressCallback callback)
{{
    {set_binary_data_decompress_callback_switch_statement}
}}

void SetBinaryDataDir(uint32_t test, const char *dataDir)
{{
    {set_binary_data_dir_switch_statement}
}}
"""


class Logger():

    def __init__(self, output_to_file, result_file):
        self.output_to_file_string = ""
        self.output_to_file = output_to_file
        self.result_file = result_file

    def Log(self, string):
        if string != None:
            print(string)
            self.output_to_file_string += string
            self.output_to_file_string += "\n"

    def End(self):
        if self.output_to_file:
            f = open(os.path.join(REPLAY_SAMPLE_FOLDER, self.result_file), "w")
            f.write(self.output_to_file_string)
            f.close()


class SubProcess():

    def __init__(self, command, to_main_stdout):
        parsed_command = shlex.split(command)
        # shell=False so that only 1 subprocess is spawned.
        # if shell=True, a shell probess is spawned, which in turn spawns the process running
        # the command. Since we do not have a handle to the 2nd process, we cannot terminate it.
        if not to_main_stdout:
            self.proc_handle = subprocess.Popen(
                parsed_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=False)
        else:
            self.proc_handle = subprocess.Popen(parsed_command, shell=False)

    def BlockingRun(self, timeout):
        output = self.proc_handle.communicate(timeout=timeout)[0]
        if output != None:
            output = output.decode("utf-8")
        else:
            output = ""
        return self.proc_handle.returncode, output

    def Pid(self):
        return self.proc_handle.pid

    def Kill(self):
        self.proc_handle.terminate()
        self.proc_handle.wait()


# class that manages all child processes of a process. Any process thats spawns subprocesses
# should have this. This object is created inside the main process, and each worker process.
class ChildProcessesManager():

    def __init__(self):
        # a dictionary of Subprocess, with pid as key
        self.subprocesses = {}
        # list of Python multiprocess.Process handles
        self.workers = []

    def CreateSubprocess(self, command, to_main_stdout):
        subprocess = SubProcess(command, to_main_stdout)
        self.subprocesses[subprocess.Pid()] = subprocess
        return subprocess.Pid()

    def RunSubprocessBlocking(self, subprocess_id, timeout=None):
        assert subprocess_id in self.subprocesses
        try:
            returncode, output = self.subprocesses[subprocess_id].BlockingRun(timeout)
            self.RemoveSubprocess(subprocess_id)
            if returncode != 0:
                return -1, output
            return returncode, output
        except KeyboardInterrupt:
            raise
        except subprocess.TimeoutExpired as e:
            self.RemoveSubprocess(subprocess_id)
            return -2, str(e)
        except Exception as e:
            self.RemoveSubprocess(subprocess_id)
            return -1, str(e)

    def RemoveSubprocess(self, subprocess_id):
        assert subprocess_id in self.subprocesses
        self.subprocesses[subprocess_id].Kill()
        del self.subprocesses[subprocess_id]

    def AddWorker(self, worker):
        self.workers.append(worker)

    def KillAll(self):
        for subprocess_id in self.subprocesses:
            self.subprocesses[subprocess_id].Kill()
        for worker in self.workers:
            worker.terminate()
            worker.join()
            worker.close()  # to release file descriptors immediately
        self.subprocesses = {}
        self.workers = []

    def JoinWorkers(self):
        for worker in self.workers:
            worker.join()
            worker.close()
        self.workers = []

    def IsAnyWorkerAlive(self):
        return any([worker.is_alive() for worker in self.workers])

    def GetRemainingWorkers(self):
        count = 0
        for worker in self.workers:
            if worker.is_alive():
                count += 1
        return count


def CreateGNGenCommand(gn_path, build_dir, arguments):
    command = '"' + gn_path + '"' + ' gen --args="'
    is_first_argument = True
    for argument in arguments:
        if is_first_argument:
            is_first_argument = False
        else:
            command += ' '
        command += argument[0]
        command += '='
        command += argument[1]
    command += '" '
    command += build_dir
    return command


def CreateAutoninjaCommand(autoninja_path, build_dir, target):
    command = '"' + autoninja_path + '" '
    command += target
    command += " -C "
    command += build_dir
    return command


def GetGnAndAutoninjaAbsolutePaths():
    # get gn/autoninja absolute path because subprocess with shell=False doesn't look
    # into the PATH environment variable on Windows
    depot_tools_name = "depot_tools"
    if platform == "win32":
        paths = os.environ["PATH"].split(";")
    else:
        paths = os.environ["PATH"].split(":")
    for path in paths:
        if path.endswith(depot_tools_name):
            if platform == "win32":
                return os.path.join(path, "gn.bat"), os.path.join(path, "autoninja.bat")
            else:
                return os.path.join(path, "gn"), os.path.join(path, "autoninja")
    return "", ""


def GetTestNamesAndParamsCommand(test_exec_path, filter="*"):
    return '"' + test_exec_path + '" --gtest_list_tests --gtest_filter=' + filter


def ProcessGetTestNamesAndParamsCommandOutput(output):

    def SkipTest(skipped_test_patterns, full_test_name):
        for skipped_test_pattern in skipped_test_patterns:
            if fnmatch.fnmatch(full_test_name, skipped_test_pattern):
                return True
        return False

    output_lines = output.splitlines()
    tests = []
    last_testcase_name = ""
    test_name_splitter = "# GetParam() ="
    test_expectations_filename = "capture_replay_expectations.txt"
    skipped_test_patterns = [
        line.rstrip() for line in \
        open(os.path.join(REPLAY_SAMPLE_FOLDER, test_expectations_filename), "r") \
        if (not line.startswith("#") and not line.isspace())
    ]
    for line in output_lines:
        if test_name_splitter in line:
            # must be a test name line
            test_name_and_params = line.split(test_name_splitter)
            full_test_name = last_testcase_name + test_name_and_params[0].strip()
            if not SkipTest(skipped_test_patterns, full_test_name):
                tests.append((full_test_name, test_name_and_params[1].strip()))
        else:
            # gtest_list_tests returns the test in this format
            # test case
            #    test name1
            #    test name2
            # Need to remember the last test case name to append to the test name
            last_testcase_name = line
    return tests


def WriteGeneratedSwitchStatements(tests, call, params, returns=False, default_val=""):
    call_lists = []
    call_splitter = "Context"
    for test in tests:
        actual_call_name = call
        if call_splitter in call:
            # split the call so we can put the context id in the middle
            prefix, suffix = call.split(call_splitter)
            actual_call_name = prefix + call_splitter + str(test.context_id) + suffix
        call_lists.append(actual_call_name)

    switch_cases = "".join(
        [
            switch_case_with_return_template.format(
                case=str(i), namespace=tests[i].GetLabel(), call=call_lists[i], params=params) if returns
            else switch_case_without_return_template.format(
                case=str(i), namespace=tests[i].GetLabel(), call=call_lists[i], params=params) \
            for i in range(len(tests))
        ]
    )
    default_case = default_case_with_return_template.format(default_val=default_val) if returns \
        else default_case_without_return_template
    return switch_statement_template.format(switch_cases=switch_cases, default_case=default_case)


def WriteAngleTraceGLHeader():
    f = open(os.path.join(REPLAY_SAMPLE_FOLDER, "angle_trace_gl.h"), "w")
    f.write("""#ifndef ANGLE_TRACE_GL_H_
#define ANGLE_TRACE_GL_H_

#include "util/util_gl.h"

#endif  // ANGLE_TRACE_GL_H_
""")
    f.close()


class GroupedResult():
    Passed = "Passed"
    Failed = "Failed"
    TimedOut = "Timeout"
    Crashed = "Crashed"
    CompileFailed = "CompileFailed"
    Skipped = "Skipped"


    def __init__(self, resultcode, message, output, tests):
        self.resultcode = resultcode
        self.message = message
        self.output = output
        self.tests = []
        for test in tests:
            self.tests.append(test)


class TestBatchResult():

    display_output_lines = 20

    def __init__(self, grouped_results, verbose):
        self.passes = []
        self.fails = []
        self.timeouts = []
        self.crashes = []
        self.compile_fails = []
        self.skips = []

        for grouped_result in grouped_results:
            if grouped_result.resultcode == GroupedResult.Passed:
                for test in grouped_result.tests:
                    self.passes.append(test.full_test_name)
            elif grouped_result.resultcode == GroupedResult.Failed:
                for test in grouped_result.tests:
                    self.fails.append(test.full_test_name)
            elif grouped_result.resultcode == GroupedResult.TimedOut:
                for test in grouped_result.tests:
                    self.timeouts.append(test.full_test_name)
            elif grouped_result.resultcode == GroupedResult.Crashed:
                for test in grouped_result.tests:
                    self.crashes.append(test.full_test_name)
            elif grouped_result.resultcode == GroupedResult.CompileFailed:
                for test in grouped_result.tests:
                    self.compile_fails.append(test.full_test_name)
            elif grouped_result.resultcode == GroupedResult.Skipped:
                for test in grouped_result.tests:
                    self.skips.append(test.full_test_name)

        self.repr_str = ""
        self.GenerateRepresentationString(grouped_results, verbose)

    def __str__(self):
        return self.repr_str

    def GenerateRepresentationString(self, grouped_results, verbose):
        for grouped_result in grouped_results:
            self.repr_str += grouped_result.resultcode + ": " + grouped_result.message + "\n"
            for test in grouped_result.tests:
                self.repr_str += "\t" + test.full_test_name + "\n"
            if verbose:
                self.repr_str += grouped_result.output
            else:
                if grouped_result.resultcode == GroupedResult.CompileFailed:
                    self.repr_str += TestBatchResult.ExtractErrors(grouped_result.output)
                else:
                    self.repr_str += TestBatchResult.GetAbbreviatedOutput(grouped_result.output)

    def ExtractErrors(output):
        lines = output.splitlines()
        error_lines = []
        for i in range(len(lines)):
            if ": error:" in lines[i]:
                error_lines.append(lines[i] + "\n")
                if i + 1 < len(lines):
                    error_lines.append(lines[i + 1] + "\n")
        return "".join(error_lines)

    def GetAbbreviatedOutput(output):
        lines = output.splitlines()
        return "".join([lines[-1 - i] + "\n" \
            for i in range(min(len(lines), TestBatchResult.display_output_lines)-1, -1, -1)])


class Test():

    def __init__(self, full_test_name, params):
        self.full_test_name = full_test_name
        self.params = params
        self.context_id = 0
        self.test_index = -1  # index of test within a test batch

    def __str__(self):
        return self.full_test_name + " Params: " + self.params

    def GetLabel(self):
        return self.full_test_name.replace(".", "_").replace("/", "_")

    def CanRunReplay(self, trace_folder_path):
        test_files = []
        label = self.GetLabel()
        assert (self.context_id == 0)
        for f in os.listdir(trace_folder_path):
            if os.path.isfile(os.path.join(trace_folder_path, f)) and f.startswith(label):
                test_files.append(f)
        frame_files_count = 0
        context_header_count = 0
        context_source_count = 0
        source_txt_count = 0
        context_id = 0
        for f in test_files:
            if "_frame" in f:
                frame_files_count += 1
            elif f.endswith(".txt"):
                source_txt_count += 1
            elif f.endswith(".h"):
                context_header_count += 1
                context_id = int(f.split("capture_context")[1][:-2])
            elif f.endswith(".cpp"):
                context_source_count += 1
        can_run_replay = frame_files_count >= 1 and context_header_count == 1 \
            and context_source_count == 1 and source_txt_count == 1
        if not can_run_replay:
            return False
        self.context_id = context_id
        return True


class TestBatch():

    def __init__(self, use_goma, batch_count, keep_temp_files, goma_dir, verbose):
        self.use_goma = use_goma
        self.tests = []
        self.batch_count = batch_count
        self.keep_temp_files = keep_temp_files
        self.goma_dir = goma_dir
        self.verbose = verbose
        self.results = []

    def Run(self, test_exe_path, trace_folder_path, child_processes_manager):
        os.environ["ANGLE_CAPTURE_ENABLED"] = "1"
        if not self.keep_temp_files:
            ClearFolderContent(trace_folder_path)
        command = '"' + test_exe_path + '" --gtest_filter='
        for test in self.tests:
            command += test.full_test_name
            if test.test_index != len(self.tests) - 1:
                command += ':'
        command += " --angle-per-test-capture-label"
        capture_proc_id = child_processes_manager.CreateSubprocess(command, False)
        returncode, output = child_processes_manager.RunSubprocessBlocking(
            capture_proc_id, SUBPROCESS_TIMEOUT)
        if returncode == -1:
            self.results.append(
                GroupedResult(GroupedResult.Crashed, "Capture run crashed", output, self.tests))
            return False
        elif returncode == -2:
            self.results.append(
                GroupedResult(GroupedResult.TimedOut, "Capture run timed out", "", self.tests))
            return False
        return True

    def RemoveTestsThatDoNotProduceAppropriateTraceFiles(self, trace_folder_path):
        continued_tests = []
        skipped_tests = []
        for test in self.tests:
            if not test.CanRunReplay(trace_folder_path):
                skipped_tests.append(test)
            else:
                continued_tests.append(test)
        if len(skipped_tests) > 0:
            self.results.append(
                GroupedResult(
                    GroupedResult.Skipped,
                    "Skipping replay since capture didn't produce necessary trace files", "",
                    skipped_tests))
        return continued_tests

    def BuildReplay(self, gn_path, autoninja_path, build_dir, trace_dir, replay_exec,
                    trace_folder_path, composite_file_id, tests, child_processes_manager):
        # write gni file that holds all the traces files in a list
        self.CreateGNIFile(trace_folder_path, composite_file_id, tests)
        # write header and cpp composite files, which glue the trace files with
        # CaptureReplayTests.cpp
        self.CreateTestsCompositeFiles(trace_folder_path, composite_file_id, tests)
        gn_args = [("use_goma", self.use_goma), ("angle_build_capture_replay_tests", "true"),
                   ("angle_capture_replay_test_trace_dir", '\\"' + trace_dir + '\\"'),
                   ("angle_with_capture_by_default", "false"),
                   ("angle_capture_replay_composite_file_id", str(composite_file_id))]
        if self.goma_dir != "":
            gn_args.append(("goma_dir", self.goma_dir))
        gn_command = CreateGNGenCommand(gn_path, build_dir, gn_args)
        gn_proc_id = child_processes_manager.CreateSubprocess(gn_command, False)
        returncode, output = child_processes_manager.RunSubprocessBlocking(gn_proc_id)
        if returncode != 0:
            self.results.append(
                GroupedResult(GroupedResult.CompileFailed, "Build replay failed at gn generation",
                              output, tests))
            return False
        autoninja_command = CreateAutoninjaCommand(autoninja_path, build_dir, replay_exec)
        autoninja_proc_id = child_processes_manager.CreateSubprocess(autoninja_command, False)
        returncode, output = child_processes_manager.RunSubprocessBlocking(autoninja_proc_id)
        if returncode != 0:
            self.results.append(
                GroupedResult(GroupedResult.CompileFailed, "Build replay failed at autoninja",
                              output, tests))
            return False
        return True

    def RunReplay(self, replay_exe_path, child_processes_manager, tests):
        os.environ["ANGLE_CAPTURE_ENABLED"] = "0"
        command = '"' + replay_exe_path + '"'
        replay_proc_id = child_processes_manager.CreateSubprocess(command, False)
        returncode, output = child_processes_manager.RunSubprocessBlocking(
            replay_proc_id, SUBPROCESS_TIMEOUT)
        if returncode == -1:
            self.results.append(
                GroupedResult(GroupedResult.Crashed, "Replay run crashed", output, tests))
            return
        elif returncode == -2:
            self.results.append(
                GroupedResult(GroupedResult.TimedOut, "Replay run timed out", "", tests))
            return
        output_lines = output.splitlines()
        passes = []
        fails = []
        count = 0
        for output_line in output_lines:
            words = output_line.split(" ")
            if len(words) == 3 and words[0] == RESULT_TAG:
                if int(words[2]) == 0:
                    passes.append(self.FindTestByLabel(words[1]))
                else:
                    fails.append(self.FindTestByLabel(words[1]))
                count += 1
        if len(passes) > 0:
            self.results.append(GroupedResult(GroupedResult.Passed, "", "", passes))
        if len(fails) > 0:
            self.results.append(GroupedResult(GroupedResult.Failed, "", "", fails))

    def FindTestByLabel(self, label):
        for test in self.tests:
            if test.GetLabel() == label:
                return test
        return None

    def AddTest(self, test):
        assert len(self.tests) <= self.batch_count
        test.index = len(self.tests)
        self.tests.append(test)

    # gni file, which holds all the sources for a replay application
    def CreateGNIFile(self, trace_folder_path, composite_file_id, tests):
        capture_sources = []
        for test in tests:
            label = test.GetLabel()
            assert (test.context_id > 0)
            trace_file_suffix = TRACE_FILE_SUFFIX + str(test.context_id)
            trace_files = [label + trace_file_suffix + ".h", label + trace_file_suffix + ".cpp"]
            try:
                # reads from {label}_capture_context1_files.txt and adds the traces files recorded
                # in there to the list of trace files
                f = open(os.path.join(trace_folder_path, label + trace_file_suffix + "_files.txt"))
                trace_files += f.read().splitlines()
                f.close()
            except IOError:
                continue
            capture_sources += trace_files
        f = open(os.path.join(trace_folder_path, "traces" + str(composite_file_id) + ".gni"), "w")
        f.write("generated_sources = [\n")
        # write the list of trace files to the gni file
        for filename in capture_sources:
            f.write('    "' + filename + '",\n')
        f.write("]")
        f.close()

    # header and cpp composite files, which glue the trace files with CaptureReplayTests.cpp
    def CreateTestsCompositeFiles(self, trace_folder_path, composite_file_id, tests):
        # write CompositeTests header file
        h_filename = "CompositeTests" + str(composite_file_id) + ".h"
        h_file = open(os.path.join(trace_folder_path, h_filename), "w")

        include_header_template = '#include "{header_file_path}.h"\n'
        trace_headers = "".join([
            include_header_template.format(header_file_path=test.GetLabel() + TRACE_FILE_SUFFIX +
                                           str(test.context_id)) for test in tests
        ])
        h_file.write(composite_h_file_template.format(trace_headers=trace_headers))
        h_file.close()

        # write CompositeTests cpp file
        cpp_file = open(
            os.path.join(trace_folder_path, "CompositeTests" + str(composite_file_id) + ".cpp"),
            "w")

        test_trace_info_inits = "".join([
            test_trace_info_init_template.format(namespace=tests[i].GetLabel())
            for i in range(len(tests))
        ])
        setup_context1_replay_switch_statement = WriteGeneratedSwitchStatements(
            tests, "SetupContextReplay", "")
        replay_context1_replay_switch_statement = WriteGeneratedSwitchStatements(
            tests, "ReplayContextFrame", "frameIndex")
        reset_context1_replay_switch_statement = WriteGeneratedSwitchStatements(
            tests, "ResetContextReplay", "")
        get_serialized_context1_state_data_switch_statement = WriteGeneratedSwitchStatements(
            tests, "GetSerializedContextStateData", "frameIndex", True, "{}")
        set_binary_data_decompress_callback_switch_statement = WriteGeneratedSwitchStatements(
            tests, "SetBinaryDataDecompressCallback", "callback")
        set_binary_data_dir_switch_statement = WriteGeneratedSwitchStatements(
            tests, "SetBinaryDataDir", "dataDir")
        cpp_file.write(
            composite_cpp_file_template.format(
                h_filename=h_filename,
                test_trace_info_inits=test_trace_info_inits,
                setup_context1_replay_switch_statement=setup_context1_replay_switch_statement,
                replay_context1_replay_switch_statement=replay_context1_replay_switch_statement,
                reset_context1_replay_switch_statement=reset_context1_replay_switch_statement,
                get_serialized_context1_state_data_switch_statement=get_serialized_context1_state_data_switch_statement,
                set_binary_data_decompress_callback_switch_statement=set_binary_data_decompress_callback_switch_statement,
                set_binary_data_dir_switch_statement=set_binary_data_dir_switch_statement))
        cpp_file.close()

    def __str__(self):
        repr_str = "TestBatch:\n"
        for test in self.tests:
            repr_str += ("\t" + str(test) + "\n")
        return repr_str

    def __getitem__(self, index):
        assert index < len(self.tests)
        return self.tests[index]

    def __iter__(self):
        return iter(self.tests)

    def GetResults(self):
        return TestBatchResult(self.results, self.verbose)


def ClearFolderContent(path):
    all_files = []
    for f in os.listdir(path):
        if os.path.isfile(os.path.join(path, f)):
            os.remove(os.path.join(path, f))

def SetCWDToAngleFolder():
    angle_folder = "angle"
    cwd = os.path.dirname(os.path.abspath(__file__))
    cwd = cwd.split(angle_folder)[0] + angle_folder
    os.chdir(cwd)
    return cwd


def RunTests(worker_id, job_queue, gn_path, autoninja_path, capture_build_dir, replay_build_dir,
             test_exec, replay_exec, trace_dir, result_list, message_queue):
    trace_folder_path = os.path.join(REPLAY_SAMPLE_FOLDER, trace_dir)
    test_exec_path = os.path.join(capture_build_dir, test_exec)
    replay_exec_path = os.path.join(replay_build_dir, replay_exec)

    os.environ["ANGLE_CAPTURE_OUT_DIR"] = trace_folder_path
    child_processes_manager = ChildProcessesManager()
    # used to differentiate between multiple composite files when there are multiple test batchs
    # running on the same worker and --deleted_trace is set to False
    composite_file_id = 1
    while not job_queue.empty():
        try:
            test_batch = job_queue.get()
            message_queue.put("Starting {} tests on worker {}. Unstarted jobs: {}".format(
                len(test_batch.tests), worker_id, job_queue.qsize()))
            success = test_batch.Run(test_exec_path, trace_folder_path, child_processes_manager)
            if not success:
                result_list.append(test_batch.GetResults())
                message_queue.put(str(test_batch.GetResults()))
                continue
            continued_tests = test_batch.RemoveTestsThatDoNotProduceAppropriateTraceFiles(
                trace_folder_path)
            if len(continued_tests) == 0:
                result_list.append(test_batch.GetResults())
                message_queue.put(str(test_batch.GetResults()))
                continue
            success = test_batch.BuildReplay(gn_path, autoninja_path, replay_build_dir, trace_dir,
                                             replay_exec, trace_folder_path, composite_file_id,
                                             continued_tests, child_processes_manager)
            if test_batch.keep_temp_files:
                composite_file_id += 1
            if not success:
                result_list.append(test_batch.GetResults())
                message_queue.put(str(test_batch.GetResults()))
                continue
            test_batch.RunReplay(replay_exec_path, child_processes_manager, continued_tests)
            result_list.append(test_batch.GetResults())
            message_queue.put(str(test_batch.GetResults()))
        except KeyboardInterrupt:
            child_processes_manager.KillAll()
            raise
        except queue.Empty:
            child_processes_manager.KillAll()
            break
        except Exception as e:
            message_queue.put("RunTestsException: " + str(e))
            child_processes_manager.KillAll()
            pass
    child_processes_manager.KillAll()


def CreateReplayBuildFolders(folder_num, replay_build_dir):
    for i in range(folder_num):
        replay_build_dir_name = replay_build_dir + str(i)
        if os.path.isdir(replay_build_dir_name):
            shutil.rmtree(replay_build_dir_name)
        os.makedirs(replay_build_dir_name)


def SafeDeleteFolder(folder_name):
    while os.path.isdir(folder_name):
        try:
            shutil.rmtree(folder_name)
        except KeyboardInterrupt:
            raise
        except PermissionError:
            pass


def DeleteReplayBuildFolders(folder_num, replay_build_dir, trace_folder):
    for i in range(folder_num):
        folder_name = replay_build_dir + str(i)
        if os.path.isdir(folder_name):
            SafeDeleteFolder(folder_name)


def CreateTraceFolders(folder_num, trace_folder):
    for i in range(folder_num):
        folder_name = trace_folder + str(i)
        folder_path = os.path.join(REPLAY_SAMPLE_FOLDER, folder_name)
        if os.path.isdir(folder_path):
            shutil.rmtree(folder_path)
        os.makedirs(folder_path)


def DeleteTraceFolders(folder_num, trace_folder):
    for i in range(folder_num):
        folder_name = trace_folder + str(i)
        folder_path = os.path.join(REPLAY_SAMPLE_FOLDER, folder_name)
        if os.path.isdir(folder_path):
            SafeDeleteFolder(folder_path)


def main(capture_build_dir, replay_build_dir, use_goma, gtest_filter, test_exec, batch_count,
         keep_temp_files, goma_dir, output_to_file, result_file, verbose):
    logger = Logger(output_to_file, result_file)
    child_processes_manager = ChildProcessesManager()
    try:
        start_time = time.time()
        # set the number of workers to be cpu_count - 1 (since the main process already takes up a
        # CPU core). Whenever a worker is available, it grabs the next job from the job queue and
        # runs it. The worker closes down when there is no more job.
        worker_count = multiprocessing.cpu_count() - 1
        cwd = SetCWDToAngleFolder()
        # create traces and build folders
        trace_folder = "traces"
        if not os.path.isdir(capture_build_dir):
            os.makedirs(capture_build_dir)
        CreateReplayBuildFolders(worker_count, replay_build_dir)
        CreateTraceFolders(worker_count, trace_folder)
        WriteAngleTraceGLHeader()
        replay_exec = "capture_replay_tests"
        if platform == "win32":
            test_exec += ".exe"
            replay_exec += ".exe"
        gn_path, autoninja_path = GetGnAndAutoninjaAbsolutePaths()
        if gn_path == "" or autoninja_path == "":
            logger.Log("No gn or autoninja found on system")
            logger.End()
            return
        # generate gn files
        gn_args = [("use_goma", use_goma), ("angle_with_capture_by_default", "true")]
        if goma_dir != "":
            gn_args.append(("goma_dir", goma_dir))
        gn_command = CreateGNGenCommand(gn_path, capture_build_dir, gn_args)
        gn_proc_id = child_processes_manager.CreateSubprocess(gn_command, True)
        returncode, output = child_processes_manager.RunSubprocessBlocking(gn_proc_id)
        if returncode != 0:
            logger.Log(output)
            logger.End()
            child_processes_manager.KillAll()
            return
        # run autoninja to build all tests
        autoninja_command = CreateAutoninjaCommand(autoninja_path, capture_build_dir, test_exec)
        autoninja_proc_id = child_processes_manager.CreateSubprocess(autoninja_command, True)
        returncode, output = child_processes_manager.RunSubprocessBlocking(autoninja_proc_id)
        if returncode != 0:
            logger.Log(output)
            logger.End()
            child_processes_manager.KillAll()
            return
        # get a list of tests
        get_tests_command = GetTestNamesAndParamsCommand(
            os.path.join(capture_build_dir, test_exec), gtest_filter)
        get_tests_command_proc_id = child_processes_manager.CreateSubprocess(
            get_tests_command, False)
        returncode, output = child_processes_manager.RunSubprocessBlocking(
            get_tests_command_proc_id)
        if returncode != 0:
            logger.Log(output)
            logger.End()
            child_processes_manager.KillAll()
            return
        test_names_and_params = ProcessGetTestNamesAndParamsCommandOutput(output)
        # objects created by manager can be shared by multiple processes. We use it to create
        # collections that are shared by multiple processes such as job queue or result list.
        manager = multiprocessing.Manager()
        job_queue = manager.Queue()
        test_batch_num = int(math.ceil(len(test_names_and_params) / float(batch_count)))

        # put the test batchs into the job queue
        for batch_index in range(test_batch_num):
            batch = TestBatch(use_goma, batch_count, keep_temp_files, goma_dir, verbose)
            test_index = batch_index
            while test_index < len(test_names_and_params):
                batch.AddTest(
                    Test(test_names_and_params[test_index][0],
                         test_names_and_params[test_index][1]))
                test_index += test_batch_num
            job_queue.put(batch)

        # set the static environment variables that do not change throughout the script run
        environment_vars = [("ANGLE_CAPTURE_FRAME_END", "100"),
                            ("ANGLE_CAPTURE_SERIALIZE_STATE", "1")]
        for environment_var in environment_vars:
            os.environ[environment_var[0]] = environment_var[1]

        passed_count = 0
        failed_count = 0
        timedout_count = 0
        crashed_count = 0
        compile_failed_count = 0
        skipped_count = 0

        failed_tests = []
        timed_out_tests = []
        crashed_tests = []
        compile_failed_tests = []
        skipped_tests = []


        # result list is created by manager and can be shared by multiple processes. Each
        # subprocess populates the result list with the results of its test runs. After all
        # subprocesses finish, the main process processes the results in the result list.
        # An item in the result list is a tuple with 3 values (testname, result, output).
        # The "result" can take 3 values "Passed", "Failed", "Skipped". The output is the
        # stdout and the stderr of the test appended together.
        result_list = manager.list()
        message_queue = manager.Queue()
        # so that we do not spawn more processes than we actually need
        worker_count = min(worker_count, test_batch_num)
        # spawning and starting up workers
        for i in range(worker_count):
            proc = multiprocessing.Process(
                target=RunTests,
                args=(i, job_queue, gn_path, autoninja_path, capture_build_dir,
                      replay_build_dir + str(i), test_exec, replay_exec, trace_folder + str(i),
                      result_list, message_queue))
            child_processes_manager.AddWorker(proc)
            proc.start()

        # print out messages from the message queue populated by workers
        # if there is no message, and the elapsed time between now and when the last message is
        # print exceeds TIME_BETWEEN_MESSAGE, prints out a message to signal that tests are still
        # running
        last_message_timestamp = 0
        while child_processes_manager.IsAnyWorkerAlive():
            while not message_queue.empty():
                msg = message_queue.get()
                logger.Log(msg)
                last_message_timestamp = time.time()
            cur_time = time.time()
            if cur_time - last_message_timestamp > TIME_BETWEEN_MESSAGE:
                last_message_timestamp = cur_time
                logger.Log("Tests are still running. Remaining workers: " + \
                str(child_processes_manager.GetRemainingWorkers()) + \
                ". Unstarted jobs: " + str(job_queue.qsize()))
            time.sleep(1.0)
        child_processes_manager.JoinWorkers()
        while not message_queue.empty():
            msg = message_queue.get()
            logger.Log(msg)
        # delete the static environment variables
        for environment_var in environment_vars:
            del os.environ[environment_var[0]]
        end_time = time.time()

        # print out results
        logger.Log("\n\n\n")
        logger.Log("Results:")
        for test_batch_result in result_list:
            logger.Log(str(test_batch_result))
            passed_count += len(test_batch_result.passes)
            failed_count += len(test_batch_result.fails)
            timedout_count += len(test_batch_result.timeouts)
            crashed_count += len(test_batch_result.crashes)
            compile_failed_count += len(test_batch_result.compile_fails)
            skipped_count += len(test_batch_result.skips)


            for failed_test in test_batch_result.fails:
                failed_tests.append(failed_test)
            for timeout_test in test_batch_result.timeouts:
                timed_out_tests.append(timeout_test)
            for crashed_test in test_batch_result.crashes:
                crashed_tests.append(crashed_test)
            for compile_failed_test in test_batch_result.compile_fails:
                compile_failed_tests.append(compile_failed_test)
            for skipped_test in test_batch_result.skips:
                skipped_tests.append(skipped_test)
        logger.Log("\n\n")
        logger.Log("Elapsed time: " + str(end_time - start_time) + " seconds")
        logger.Log("Passed: "+ str(passed_count) + " Failed: " + str(failed_count) + \
        " Crashed: " + str(crashed_count) + " CompileFailed: " + str(compile_failed_count) + \
        " Skipped: " + str(skipped_count) + " Timeout: " + str(timedout_count))
        logger.Log("Failed tests:")
        for failed_test in failed_tests:
            logger.Log("\t" + failed_test)
        logger.Log("Crashed tests:")
        for crashed_test in crashed_tests:
            logger.Log("\t" + crashed_test)
        logger.Log("Compile failed tests:")
        for compile_failed_test in compile_failed_tests:
            logger.Log("\t" + compile_failed_test)
        logger.Log("Skipped tests:")
        for skipped_test in skipped_tests:
            logger.Log("\t" + skipped_test)
        logger.Log("Timeout tests:")
        for timeout_test in timed_out_tests:
            logger.Log("\t" + timeout_test)

        # delete generated folders if --keep_temp_files flag is set to false
        if not keep_temp_files:
            os.remove(os.path.join(REPLAY_SAMPLE_FOLDER, "angle_trace_gl.h"))
            DeleteTraceFolders(worker_count, trace_folder)
            DeleteReplayBuildFolders(worker_count, replay_build_dir, trace_folder)
        if not keep_temp_files and os.path.isdir(capture_build_dir):
            SafeDeleteFolder(capture_build_dir)
        logger.End()
    except KeyboardInterrupt:
        child_processes_manager.KillAll()
        logger.End()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--capture-build-dir', default=DEFAULT_CAPTURE_BUILD_DIR)
    parser.add_argument('--replay-build-dir', default=DEFAULT_REPLAY_BUILD_DIR)
    parser.add_argument('--use-goma', action='store_true')
    parser.add_argument('--gtest_filter', default=DEFAULT_FILTER)
    parser.add_argument('--test-suite', default=DEFAULT_TEST_SUITE)
    parser.add_argument('--batch-count', default=DEFAULT_BATCH_COUNT)
    parser.add_argument('--keep-temp-files', action='store_true')
    parser.add_argument("--goma-dir", default="")
    parser.add_argument("--output-to-file", action='store_true')
    parser.add_argument("--result-file", default=DEFAULT_RESULT_FILE)
    parser.add_argument("--verbose", action='store_true')
    args = parser.parse_args()
    main(args.capture_build_dir, args.replay_build_dir,
         str(args.use_goma).lower(), args.gtest_filter, args.test_suite, int(args.batch_count),
         args.keep_temp_files, args.goma_dir, args.output_to_file, args.result_file, args.verbose)
