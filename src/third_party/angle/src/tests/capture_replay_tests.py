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
# --build_dir: specifies build directory relative to angle folder.
# Default is out/CaptureReplayTestsDebug
# --verbose: off by default
# --use_goma: uses goma for compiling and linking test. Off by default
# --gtest_filter: same as gtest_filter of Google's test framework. Default is */ES2_Vulkan
# --test_suite: test suite to execute on. Default is angle_end2end_tests

import argparse
import distutils.util
import os
import shutil
import subprocess

from sys import platform

DEFAULT_BUILD_DIR = "out/CaptureReplayTestsDebug"  # relative to angle folder
DEFAULT_FILTER = "*/ES2_Vulkan"
DEFAULT_TEST_SUITE = "angle_end2end_tests"


class Logger():
    verbose = False

    @staticmethod
    def log(msg):
        if Logger.verbose:
            print(msg)


def RunGnGen(build_dir, arguments, is_log_showed=False):
    command = 'gn gen --args="'
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
    if is_log_showed:
        subprocess.call(command, shell=True)
    else:
        subprocess.check_output(command, shell=True)


def RunAutoninja(build_dir, target, is_log_showed=False):
    command = "autoninja "
    command += target
    command += " -C "
    command += build_dir
    if is_log_showed:
        subprocess.call(command, shell=True)
    else:
        subprocess.check_output(command, shell=True)


# return a list of tests and their params in the form
# [(test1, params1), (test2, params2),...]
def GetTestNamesAndParams(test_exec_path, filter="*"):
    output = subprocess.check_output(
        '"' + test_exec_path + '" --gtest_list_tests --gtest_filter=' + filter,
        shell=True,
        stderr=subprocess.PIPE).splitlines()

    tests = []
    last_testcase_name = ""
    test_name_splitter = "# GetParam() ="
    for line in output:
        if test_name_splitter in line:
            # must be a test name line
            test_name_and_params = line.split(test_name_splitter)
            tests.append((last_testcase_name + test_name_and_params[0].strip(), \
                test_name_and_params[1].strip()))
        else:
            # gtest_list_tests returns the test in this format
            # test case
            #    test name1
            #    test name2
            # Need to remember the last test case name to append to the test name
            last_testcase_name = line
    return tests


class Test():

    def __init__(self, full_test_name, params, use_goma):
        self.full_test_name = full_test_name
        self.params = params
        self.use_goma = use_goma

    def __str__(self):
        return self.full_test_name + " Params: " + self.params

    def Run(self, test_exe_path):
        try:
            output = subprocess.check_output(
                '"' + test_exe_path + '" --gtest_filter=' + self.full_test_name + \
                ' --use-angle=vulkan',
                shell=True,
                stderr=subprocess.PIPE)
            Logger.log("Ran " + self.full_test_name + " with capture")
            return (0, output)
        except subprocess.CalledProcessError as e:
            return (e.returncode, e.output)

    def BuildReplay(self, build_dir, replay_exec):
        RunGnGen(build_dir, [("use_goma", self.use_goma),
                             ("angle_with_capture_by_default", "true"),
                             ("angle_build_capture_replay_tests", "true")])
        RunAutoninja(build_dir, replay_exec)
        Logger.log("Built replay of " + self.full_test_name)

    def RunReplay(self, build_dir, replay_exec):
        try:
            output = subprocess.check_output(
                '"' + build_dir + '/' + replay_exec + '" --use-angle=vulkan',
                shell=True,
                stderr=subprocess.PIPE)
            Logger.log("Ran replay of " + self.full_test_name)
            return (0, output)
        except subprocess.CalledProcessError as e:
            return (e.returncode, e.output)


def ClearFolderContent(path):
    all_files = []
    for f in os.listdir(path):
        if os.path.isfile(path + "/" + f) and f.startswith("angle_capture_context"):
            os.remove(path + "/" + f)


def CanRunReplay(path):
    files = [
        "angle_capture_context1.h", "angle_capture_context1.cpp",
        "angle_capture_context1_files.txt", "angle_capture_context1_frame000.cpp"
    ]
    for file in files:
        if not os.path.isfile(path + "/" + file):
            return False
    return True


def SetCWDToAngleFolder():
    angle_folder = "angle"
    cwd = os.path.dirname(os.path.abspath(__file__))
    cwd = cwd.split(angle_folder)[0] + angle_folder
    os.chdir(cwd)
    return cwd


def main(build_dir, verbose, use_goma, gtest_filter, test_exec):
    Logger.verbose = verbose
    cwd = SetCWDToAngleFolder()
    capture_out_dir = "src/tests/capture_replay_tests/traces"  # relative to ANGLE folder
    if not os.path.isdir(capture_out_dir):
        os.mkdir(capture_out_dir)
    environment_vars = [("ANGLE_CAPTURE_FRAME_END", "0"),
                        ("ANGLE_CAPTURE_OUT_DIR", capture_out_dir)]
    replay_exec = "capture_replay_tests"
    if platform == "win32":
        test_exec += ".exe"
        replay_exec += ".exe"
    # generate gn files
    RunGnGen(build_dir, [("use_goma", use_goma), ("angle_with_capture_by_default", "true")], True)
    # build angle_end2end
    RunAutoninja(build_dir, test_exec, True)
    # get a list of tests
    test_names_and_params = GetTestNamesAndParams(build_dir + '/' + test_exec, gtest_filter)
    all_tests = []
    for test_name_and_params in test_names_and_params:
        all_tests.append(Test(test_name_and_params[0], test_name_and_params[1], use_goma))

    for environment_var in environment_vars:
        os.environ[environment_var[0]] = environment_var[1]

    for test in all_tests:
        if verbose:
            print("*" * 30)
        ClearFolderContent(capture_out_dir)
        os.environ["ANGLE_CAPTURE_ENABLED"] = "1"
        run_output = test.Run(build_dir + "/" + test_exec)
        if run_output[0] == 0 and CanRunReplay(capture_out_dir):
            os.environ["ANGLE_CAPTURE_ENABLED"] = "0"
            test.BuildReplay(build_dir, replay_exec)
            replay_output = test.RunReplay(build_dir, replay_exec)
            if replay_output[0] != 0:
                print("Failed: " + test.full_test_name)
                print(replay_output[1])
            else:
                print("Passed: " + test.full_test_name)
        else:
            print("Skipped: " + test.full_test_name + ". Skipping replay since capture" + \
                "didn't produce appropriate files or has crashed")
    for environment_var in environment_vars:
        del os.environ[environment_var[0]]

    if os.path.isdir(capture_out_dir):
        shutil.rmtree(capture_out_dir)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--build_dir', default=DEFAULT_BUILD_DIR)
    parser.add_argument('--verbose', default="False")
    parser.add_argument('--use_goma', default="false")
    parser.add_argument('--gtest_filter', default=DEFAULT_FILTER)
    parser.add_argument('--test_suite', default=DEFAULT_TEST_SUITE)
    args = parser.parse_args()
    main(args.build_dir, distutils.util.strtobool(args.verbose), args.use_goma, args.gtest_filter,
         args.test_suite)
