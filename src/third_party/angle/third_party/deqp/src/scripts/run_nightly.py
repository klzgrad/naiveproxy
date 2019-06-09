# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#-------------------------------------------------------------------------

from build.common import *
from build.config import *
from build.build import *

import os
import sys
import string
import socket
import fnmatch
from datetime import datetime

BASE_NIGHTLY_DIR	= os.path.normpath(os.path.join(DEQP_DIR, "..", "deqp-nightly"))
BASE_BUILD_DIR		= os.path.join(BASE_NIGHTLY_DIR, "build")
BASE_LOGS_DIR		= os.path.join(BASE_NIGHTLY_DIR, "logs")
BASE_REFS_DIR		= os.path.join(BASE_NIGHTLY_DIR, "refs")

EXECUTOR_PATH		= "executor/executor"
LOG_TO_CSV_PATH		= "executor/testlog-to-csv"
EXECSERVER_PATH		= "execserver/execserver"

CASELIST_PATH		= os.path.join(DEQP_DIR, "Candy", "Data")

COMPARE_NUM_RESULTS	= 4
COMPARE_REPORT_NAME	= "nightly-report.html"

COMPARE_REPORT_TMPL = '''
<html>
<head>
<title>${TITLE}</title>
<style type="text/css">
<!--
body				{ font: serif; font-size: 1em; }
table				{ border-spacing: 0; border-collapse: collapse; }
td					{ border-width: 1px; border-style: solid; border-color: #808080; }
.Header				{ font-weight: bold; font-size: 1em; border-style: none; }
.CasePath			{ }
.Pass				{ background: #80ff80; }
.Fail				{ background: #ff4040; }
.QualityWarning		{ background: #ffff00; }
.CompabilityWarning	{ background: #ffff00; }
.Pending			{ background: #808080; }
.Running			{ background: #d3d3d3; }
.NotSupported		{ background: #ff69b4; }
.ResourceError		{ background: #ff4040; }
.InternalError		{ background: #ff1493; }
.Canceled			{ background: #808080; }
.Crash				{ background: #ffa500; }
.Timeout			{ background: #ffa500; }
.Disabled			{ background: #808080; }
.Missing			{ background: #808080; }
.Ignored			{ opacity: 0.5; }
-->
</style>
</head>
<body>
<h1>${TITLE}</h1>
<table>
${RESULTS}
</table>
</body>
</html>
'''

class NightlyRunConfig:
	def __init__(self, name, buildConfig, generator, binaryName, testset, args = [], exclude = [], ignore = []):
		self.name			= name
		self.buildConfig	= buildConfig
		self.generator		= generator
		self.binaryName		= binaryName
		self.testset		= testset
		self.args			= args
		self.exclude		= exclude
		self.ignore			= ignore

	def getBinaryPath(self, basePath):
		return os.path.join(self.buildConfig.getBuildDir(), self.generator.getBinaryPath(self.buildConfig.getBuildType(), basePath))

class NightlyBuildConfig(BuildConfig):
	def __init__(self, name, buildType, args):
		BuildConfig.__init__(self, os.path.join(BASE_BUILD_DIR, name), buildType, args)

class TestCaseResult:
	def __init__ (self, name, statusCode):
		self.name		= name
		self.statusCode	= statusCode

class MultiResult:
	def __init__ (self, name, statusCodes):
		self.name			= name
		self.statusCodes	= statusCodes

class BatchResult:
	def __init__ (self, name):
		self.name		= name
		self.results	= []

def parseResultCsv (data):
	lines	= data.splitlines()[1:]
	results	= []

	for line in lines:
		items = line.split(",")
		results.append(TestCaseResult(items[0], items[1]))

	return results

def readTestCaseResultsFromCSV (filename):
	return parseResultCsv(readFile(filename))

def readBatchResultFromCSV (filename, batchResultName = None):
	batchResult = BatchResult(batchResultName if batchResultName != None else os.path.basename(filename))
	batchResult.results = readTestCaseResultsFromCSV(filename)
	return batchResult

def getResultTimestamp ():
	return datetime.now().strftime("%Y-%m-%d-%H-%M")

def getCompareFilenames (logsDir):
	files = []
	for file in os.listdir(logsDir):
		fullPath = os.path.join(logsDir, file)
		if os.path.isfile(fullPath) and fnmatch.fnmatch(file, "*.csv"):
			files.append(fullPath)
	files.sort()

	return files[-COMPARE_NUM_RESULTS:]

def parseAsCSV (logPath, config):
	args = [config.getBinaryPath(LOG_TO_CSV_PATH), "--mode=all", "--format=csv", logPath]
	proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	out, err = proc.communicate()
	return out

def computeUnifiedTestCaseList (batchResults):
	caseList	= []
	caseSet		= set()

	for batchResult in batchResults:
		for result in batchResult.results:
			if not result.name in caseSet:
				caseList.append(result.name)
				caseSet.add(result.name)

	return caseList

def computeUnifiedResults (batchResults):

	def genResultMap (batchResult):
		resMap = {}
		for result in batchResult.results:
			resMap[result.name] = result
		return resMap

	resultMap	= [genResultMap(r) for r in batchResults]
	caseList	= computeUnifiedTestCaseList(batchResults)
	results		= []

	for caseName in caseList:
		statusCodes = []

		for i in range(0, len(batchResults)):
			result		= resultMap[i][caseName] if caseName in resultMap[i] else None
			statusCode	= result.statusCode if result != None else 'Missing'
			statusCodes.append(statusCode)

		results.append(MultiResult(caseName, statusCodes))

	return results

def allStatusCodesEqual (result):
	firstCode = result.statusCodes[0]
	for i in range(1, len(result.statusCodes)):
		if result.statusCodes[i] != firstCode:
			return False
	return True

def computeDiffResults (unifiedResults):
	diff = []
	for result in unifiedResults:
		if not allStatusCodesEqual(result):
			diff.append(result)
	return diff

def genCompareReport (batchResults, title, ignoreCases):
	class TableRow:
		def __init__ (self, testCaseName, innerHTML):
			self.testCaseName = testCaseName
			self.innerHTML = innerHTML

	unifiedResults	= computeUnifiedResults(batchResults)
	diffResults		= computeDiffResults(unifiedResults)
	rows			= []

	# header
	headerCol = '<td class="Header">Test case</td>\n'
	for batchResult in batchResults:
		headerCol += '<td class="Header">%s</td>\n' % batchResult.name
	rows.append(TableRow(None, headerCol))

	# results
	for result in diffResults:
		col = '<td class="CasePath">%s</td>\n' % result.name
		for statusCode in result.statusCodes:
			col += '<td class="%s">%s</td>\n' % (statusCode, statusCode)

		rows.append(TableRow(result.name, col))

	tableStr = ""
	for row in rows:
		if row.testCaseName is not None and matchesAnyPattern(row.testCaseName, ignoreCases):
			tableStr += '<tr class="Ignored">\n%s</tr>\n' % row.innerHTML
		else:
			tableStr += '<tr>\n%s</tr>\n' % row.innerHTML

	html = COMPARE_REPORT_TMPL
	html = html.replace("${TITLE}", title)
	html = html.replace("${RESULTS}", tableStr)

	return html

def matchesAnyPattern (name, patterns):
	for pattern in patterns:
		if fnmatch.fnmatch(name, pattern):
			return True
	return False

def statusCodesMatch (refResult, resResult):
	return refResult == 'Missing' or resResult == 'Missing' or refResult == resResult

def compareBatchResults (referenceBatch, resultBatch, ignoreCases):
	unifiedResults	= computeUnifiedResults([referenceBatch, resultBatch])
	failedCases		= []

	for result in unifiedResults:
		if not matchesAnyPattern(result.name, ignoreCases):
			refResult		= result.statusCodes[0]
			resResult		= result.statusCodes[1]

			if not statusCodesMatch(refResult, resResult):
				failedCases.append(result)

	return failedCases

def getUnusedPort ():
	# \note Not 100%-proof method as other apps may grab this port before we launch execserver
	s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	s.bind(('localhost', 0))
	addr, port = s.getsockname()
	s.close()
	return port

def runNightly (config):
	build(config.buildConfig, config.generator)

	# Run parameters
	timestamp		= getResultTimestamp()
	logDir			= os.path.join(BASE_LOGS_DIR, config.name)
	testLogPath		= os.path.join(logDir, timestamp + ".qpa")
	infoLogPath		= os.path.join(logDir, timestamp + ".txt")
	csvLogPath		= os.path.join(logDir, timestamp + ".csv")
	compareLogPath	= os.path.join(BASE_REFS_DIR, config.name + ".csv")
	port			= getUnusedPort()

	if not os.path.exists(logDir):
		os.makedirs(logDir)

	if os.path.exists(testLogPath) or os.path.exists(infoLogPath):
		raise Exception("Result '%s' already exists", timestamp)

	# Paths, etc.
	binaryName		= config.generator.getBinaryPath(config.buildConfig.getBuildType(), os.path.basename(config.binaryName))
	workingDir		= os.path.join(config.buildConfig.getBuildDir(), os.path.dirname(config.binaryName))

	execArgs = [
		config.getBinaryPath(EXECUTOR_PATH),
		'--start-server=%s' % config.getBinaryPath(EXECSERVER_PATH),
		'--port=%d' % port,
		'--binaryname=%s' % binaryName,
		'--cmdline=%s' % string.join([shellquote(arg) for arg in config.args], " "),
		'--workdir=%s' % workingDir,
		'--caselistdir=%s' % CASELIST_PATH,
		'--testset=%s' % string.join(config.testset, ","),
		'--out=%s' % testLogPath,
		'--info=%s' % infoLogPath,
		'--summary=no'
	]

	if len(config.exclude) > 0:
		execArgs += ['--exclude=%s' % string.join(config.exclude, ",")]

	execute(execArgs)

	# Translate to CSV for comparison purposes
	lastResultCsv		= parseAsCSV(testLogPath, config)
	writeFile(csvLogPath, lastResultCsv)

	if os.path.exists(compareLogPath):
		refBatchResult = readBatchResultFromCSV(compareLogPath, "reference")
	else:
		refBatchResult = None

	# Generate comparison report
	compareFilenames	= getCompareFilenames(logDir)
	batchResults		= [readBatchResultFromCSV(filename) for filename in compareFilenames]

	if refBatchResult != None:
		batchResults = [refBatchResult] + batchResults

	writeFile(COMPARE_REPORT_NAME, genCompareReport(batchResults, config.name, config.ignore))
	print "Comparison report written to %s" % COMPARE_REPORT_NAME

	# Compare to reference
	if refBatchResult != None:
		curBatchResult		= BatchResult("current")
		curBatchResult.results = parseResultCsv(lastResultCsv)
		failedCases			= compareBatchResults(refBatchResult, curBatchResult, config.ignore)

		print ""
		for result in failedCases:
			print "MISMATCH: %s: expected %s, got %s" % (result.name, result.statusCodes[0], result.statusCodes[1])

		print ""
		print "%d / %d cases passed, run %s" % (len(curBatchResult.results)-len(failedCases), len(curBatchResult.results), "FAILED" if len(failedCases) > 0 else "passed")

		if len(failedCases) > 0:
			return False

	return True

# Configurations

DEFAULT_WIN32_GENERATOR				= ANY_VS_X32_GENERATOR
DEFAULT_WIN64_GENERATOR				= ANY_VS_X64_GENERATOR

WGL_X64_RELEASE_BUILD_CFG			= NightlyBuildConfig("wgl_x64_release", "Release", ['-DDEQP_TARGET=win32_wgl'])
ARM_GLES3_EMU_X32_RELEASE_BUILD_CFG	= NightlyBuildConfig("arm_gles3_emu_release", "Release", ['-DDEQP_TARGET=arm_gles3_emu'])

BASE_ARGS							= ['--deqp-visibility=hidden', '--deqp-watchdog=enable', '--deqp-crashhandler=enable']

CONFIGS = [
	NightlyRunConfig(
		name			= "wgl_x64_release_gles2",
		buildConfig		= WGL_X64_RELEASE_BUILD_CFG,
		generator		= DEFAULT_WIN64_GENERATOR,
		binaryName		= "modules/gles2/deqp-gles2",
		args			= ['--deqp-gl-config-name=rgba8888d24s8ms0'] + BASE_ARGS,
		testset			= ["dEQP-GLES2.info.*", "dEQP-GLES2.functional.*", "dEQP-GLES2.usecases.*"],
		exclude			= [
				"dEQP-GLES2.functional.shaders.loops.*while*unconditional_continue*",
				"dEQP-GLES2.functional.shaders.loops.*while*only_continue*",
				"dEQP-GLES2.functional.shaders.loops.*while*double_continue*",
			],
		ignore			= []
		),
	NightlyRunConfig(
		name			= "wgl_x64_release_gles3",
		buildConfig		= WGL_X64_RELEASE_BUILD_CFG,
		generator		= DEFAULT_WIN64_GENERATOR,
		binaryName		= "modules/gles3/deqp-gles3",
		args			= ['--deqp-gl-config-name=rgba8888d24s8ms0'] + BASE_ARGS,
		testset			= ["dEQP-GLES3.info.*", "dEQP-GLES3.functional.*", "dEQP-GLES3.usecases.*"],
		exclude			= [
				"dEQP-GLES3.functional.shaders.loops.*while*unconditional_continue*",
				"dEQP-GLES3.functional.shaders.loops.*while*only_continue*",
				"dEQP-GLES3.functional.shaders.loops.*while*double_continue*",
			],
		ignore			= [
				"dEQP-GLES3.functional.transform_feedback.*",
				"dEQP-GLES3.functional.occlusion_query.*",
				"dEQP-GLES3.functional.lifetime.*",
				"dEQP-GLES3.functional.fragment_ops.depth_stencil.stencil_ops",
			]
		),
	NightlyRunConfig(
		name			= "wgl_x64_release_gles31",
		buildConfig		= WGL_X64_RELEASE_BUILD_CFG,
		generator		= DEFAULT_WIN64_GENERATOR,
		binaryName		= "modules/gles31/deqp-gles31",
		args			= ['--deqp-gl-config-name=rgba8888d24s8ms0'] + BASE_ARGS,
		testset			= ["dEQP-GLES31.*"],
		exclude			= [],
		ignore			= [
				"dEQP-GLES31.functional.draw_indirect.negative.command_bad_alignment_3",
				"dEQP-GLES31.functional.draw_indirect.negative.command_offset_not_in_buffer",
				"dEQP-GLES31.functional.vertex_attribute_binding.negative.bind_vertex_buffer_negative_offset",
				"dEQP-GLES31.functional.ssbo.layout.single_basic_type.packed.mediump_uint",
				"dEQP-GLES31.functional.blend_equation_advanced.basic.*",
				"dEQP-GLES31.functional.blend_equation_advanced.srgb.*",
				"dEQP-GLES31.functional.blend_equation_advanced.barrier.*",
				"dEQP-GLES31.functional.uniform_location.*",
				"dEQP-GLES31.functional.debug.negative_coverage.log.state.get_framebuffer_attachment_parameteriv",
				"dEQP-GLES31.functional.debug.negative_coverage.log.state.get_renderbuffer_parameteriv",
				"dEQP-GLES31.functional.debug.error_filters.case_0",
				"dEQP-GLES31.functional.debug.error_filters.case_2",
			]
		),
	NightlyRunConfig(
		name			= "wgl_x64_release_gl3",
		buildConfig		= WGL_X64_RELEASE_BUILD_CFG,
		generator		= DEFAULT_WIN64_GENERATOR,
		binaryName		= "modules/gl3/deqp-gl3",
		args			= ['--deqp-gl-config-name=rgba8888d24s8ms0'] + BASE_ARGS,
		testset			= ["dEQP-GL3.info.*", "dEQP-GL3.functional.*"],
		exclude			= [
				"dEQP-GL3.functional.shaders.loops.*while*unconditional_continue*",
				"dEQP-GL3.functional.shaders.loops.*while*only_continue*",
				"dEQP-GL3.functional.shaders.loops.*while*double_continue*",
			],
		ignore			= [
				"dEQP-GL3.functional.transform_feedback.*"
			]
		),
	NightlyRunConfig(
		name			= "arm_gles3_emu_x32_egl",
		buildConfig		= ARM_GLES3_EMU_X32_RELEASE_BUILD_CFG,
		generator		= DEFAULT_WIN32_GENERATOR,
		binaryName		= "modules/egl/deqp-egl",
		args			= BASE_ARGS,
		testset			= ["dEQP-EGL.info.*", "dEQP-EGL.functional.*"],
		exclude			= [
				"dEQP-EGL.functional.sharing.gles2.multithread.*",
				"dEQP-EGL.functional.multithread.*",
			],
		ignore			= []
		),
	NightlyRunConfig(
		name			= "opencl_x64_release",
		buildConfig		= NightlyBuildConfig("opencl_x64_release", "Release", ['-DDEQP_TARGET=opencl_icd']),
		generator		= DEFAULT_WIN64_GENERATOR,
		binaryName		= "modules/opencl/deqp-opencl",
		args			= ['--deqp-cl-platform-id=2 --deqp-cl-device-ids=1'] + BASE_ARGS,
		testset			= ["dEQP-CL.*"],
		exclude			= ["dEQP-CL.performance.*", "dEQP-CL.robustness.*", "dEQP-CL.stress.memory.*"],
		ignore			= [
				"dEQP-CL.scheduler.random.*",
				"dEQP-CL.language.set_kernel_arg.random_structs.*",
				"dEQP-CL.language.builtin_function.work_item.invalid_get_global_offset",
				"dEQP-CL.language.call_function.arguments.random_structs.*",
				"dEQP-CL.language.call_kernel.random_structs.*",
				"dEQP-CL.language.inf_nan.nan.frexp.float",
				"dEQP-CL.language.inf_nan.nan.lgamma_r.float",
				"dEQP-CL.language.inf_nan.nan.modf.float",
				"dEQP-CL.language.inf_nan.nan.sqrt.float",
				"dEQP-CL.api.multithread.*",
				"dEQP-CL.api.callback.random.nested.*",
				"dEQP-CL.api.memory_migration.out_of_order_host.image2d.single_device_kernel_migrate_validate_abb",
				"dEQP-CL.api.memory_migration.out_of_order.image2d.single_device_kernel_migrate_kernel_validate_abbb",
				"dEQP-CL.image.addressing_filtering12.1d_array.*",
				"dEQP-CL.image.addressing_filtering12.2d_array.*"
			]
		)
]

if __name__ == "__main__":
	config = None

	if len(sys.argv) == 2:
		cfgName = sys.argv[1]
		for curCfg in CONFIGS:
			if curCfg.name == cfgName:
				config = curCfg
				break

	if config != None:
		isOk = runNightly(config)
		if not isOk:
			sys.exit(-1)
	else:
		print "%s: [config]" % sys.argv[0]
		print ""
		print "  Available configs:"
		for config in CONFIGS:
			print "    %s" % config.name
		sys.exit(-1)
