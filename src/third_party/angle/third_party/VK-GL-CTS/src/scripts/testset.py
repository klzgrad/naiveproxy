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

import sys
import random
import string
import subprocess
from optparse import OptionParser

def all (results, predicate):
	for result in results:
		if not predicate(result):
			return False
	return True

def any (results, predicate):
	for result in results:
		if predicate(result):
			return True
	return False

class FilterRule:
	def __init__ (self, name, description, filters):
		self.name			= name
		self.description	= description
		self.filters		= filters

class TestCaseResult:
	def __init__ (self, name, results):
		self.name		= name
		self.results	= results

class Group:
	def __init__ (self, name):
		self.name		= name
		self.cases		= []

def readCaseList (filename):
	f = open(filename, 'rb')
	cases = []
	for line in f:
		if line[:6] == "TEST: ":
			case = line[6:].strip()
			if len(case) > 0:
				cases.append(case)
	return cases

def toResultList (caselist):
	results = []
	for case in caselist:
		results.append(TestCaseResult(case, []))
	return results

def addResultsToCaseList (caselist, results):
	resultMap	= {}
	caseListRes	= toResultList(caselist)

	for res in caseListRes:
		resultMap[res.name] = res

	for result in results:
		if result.name in resultMap:
			resultMap[result.name].results += result.results

	return caseListRes

def readTestResults (filename):
	f			= open(filename, 'rb')
	csvData		= f.read()
	csvLines	= csvData.splitlines()
	results		= []

	f.close()

	for line in csvLines[1:]:
		args = line.split(',')
		if len(args) == 1:
			continue # Ignore

		results.append(TestCaseResult(args[0], args[1:]))

	if len(results) == 0:
		raise Exception("Empty result list")

	# Sanity check for results
	numResultItems	= len(results[0].results)
	seenResults		= set()
	for result in results:
		if result.name in seenResults:
			raise Exception("Duplicate result row for test case '%s'" % result.name)
		if len(result.results) != numResultItems:
			raise Exception("Found %d results for test case '%s', expected %d" % (len(result.results), result.name, numResultItems))
		seenResults.add(result.name)

	return results

def readGroupList (filename):
	f = open(filename, 'rb')
	groups = []
	for line in f:
		group = line.strip()
		if group != "":
			groups.append(group)
	return groups

def createGroups (results, groupNames):
	groups	= []
	matched	= set()

	for groupName in groupNames:
		group = Group(groupName)
		groups.append(group)

		prefix		= groupName + "."
		prefixLen	= len(prefix)
		for case in results:
			if case.name[:prefixLen] == prefix:
				if case in matched:
					die("Case '%s' matched by multiple groups (when processing '%s')" % (case.name, group.name))
				group.cases.append(case)
				matched.add(case)

	return groups

def createLeafGroups (results):
	groups = []
	groupMap = {}

	for case in results:
		parts		= case.name.split('.')
		groupName	= string.join(parts[:-1], ".")

		if not groupName in groupMap:
			group = Group(groupName)
			groups.append(group)
			groupMap[groupName] = group
		else:
			group = groupMap[groupName]

		group.cases.append(case)

	return groups

def filterList (results, condition):
	filtered = []
	for case in results:
		if condition(case.results):
			filtered.append(case)
	return filtered

def getFilter (list, name):
	for filter in list:
		if filter.name == name:
			return filter
	return None

def getNumCasesInGroups (groups):
	numCases = 0
	for group in groups:
		numCases += len(group.cases)
	return numCases

def getCasesInSet (results, caseSet):
	filtered = []
	for case in results:
		if case in caseSet:
			filtered.append(case)
	return filtered

def selectCasesInGroups (results, groups):
	casesInGroups = set()
	for group in groups:
		for case in group.cases:
			casesInGroups.add(case)
	return getCasesInSet(results, casesInGroups)

def selectRandomSubset (results, groups, limit, seed):
	selectedCases	= set()
	numSelect		= min(limit, getNumCasesInGroups(groups))

	random.seed(seed)
	random.shuffle(groups)

	groupNdx = 0
	while len(selectedCases) < numSelect:
		group = groups[groupNdx]
		if len(group.cases) == 0:
			del groups[groupNdx]
			if groupNdx == len(groups):
				groupNdx -= 1
			continue # Try next

		selected = random.choice(group.cases)
		selectedCases.add(selected)
		group.cases.remove(selected)

		groupNdx = (groupNdx + 1) % len(groups)

	return getCasesInSet(results, selectedCases)

def die (msg):
	print(msg)
	sys.exit(-1)

# Named filter lists
FILTER_RULES = [
	FilterRule("all",			"No filtering",											[]),
	FilterRule("all-pass",		"All results must be 'Pass'",							[lambda l: all(l, lambda r: r == 'Pass')]),
	FilterRule("any-pass",		"Any of results is 'Pass'",								[lambda l: any(l, lambda r: r == 'Pass')]),
	FilterRule("any-fail",		"Any of results is not 'Pass' or 'NotSupported'",		[lambda l: not all(l, lambda r: r == 'Pass' or r == 'NotSupported')]),
	FilterRule("prev-failing",	"Any except last result is failure",					[lambda l: l[-1] == 'Pass' and not all(l[:-1], lambda r: r == 'Pass')]),
	FilterRule("prev-passing",	"Any except last result is 'Pass'",						[lambda l: l[-1] != 'Pass' and any(l[:-1], lambda r: r == 'Pass')])
]

if __name__ == "__main__":
	parser = OptionParser(usage = "usage: %prog [options] [caselist] [result csv file]")
	parser.add_option("-f", "--filter", dest="filter", default="all", help="filter rule name")
	parser.add_option("-l", "--list", action="store_true", dest="list", default=False, help="list available rules")
	parser.add_option("-n", "--num", dest="limit", default=0, help="limit number of cases")
	parser.add_option("-s", "--seed", dest="seed", default=0, help="use selected seed for random selection")
	parser.add_option("-g", "--groups", dest="groups_file", default=None, help="select cases based on group list file")

	(options, args)	= parser.parse_args()

	if options.list:
		print("Available filter rules:")
		for filter in FILTER_RULES:
			print("  %s: %s" % (filter.name, filter.description))
		sys.exit(0)

	if len(args) == 0:
		die("No input files specified")
	elif len(args) > 2:
		die("Too many arguments")

	# Fetch filter
	filter = getFilter(FILTER_RULES, options.filter)
	if filter == None:
		die("Unknown filter '%s'" % options.filter)

	# Read case list
	caselist = readCaseList(args[0])
	if len(args) > 1:
		results = readTestResults(args[1])
		results = addResultsToCaseList(caselist, results)
	else:
		results = toResultList(caselist)

	# Execute filters for results
	for rule in filter.filters:
		results = filterList(results, rule)

	if options.limit != 0:
		if options.groups_file != None:
			groups = createGroups(results, readGroupList(options.groups_file))
		else:
			groups = createLeafGroups(results)
		results = selectRandomSubset(results, groups, int(options.limit), int(options.seed))
	elif options.groups_file != None:
		groups = createGroups(results, readGroupList(options.groups_file))
		results = selectCasesInGroups(results, groups)

	# Print test set
	for result in results:
		print(result.name)
