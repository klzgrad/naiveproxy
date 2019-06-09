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

from itertools import chain
from format import indentLines

def isValueDefined (definitions, value):
	return value in definitions

def allValuesUndefined (definitions, values):
	for value in values:
		if isValueDefined(definitions, value):
			return False
	return True

def anyValueDefined (definitions, values):
	return not allValuesUndefined(definitions, values)

def makeDefSet (iface):
	return set(enum.name for enum in iface.enums) | \
		   set(enum.alias for enum in iface.enums if enum.alias != None)

def genStrUtilProtos (iface, enumGroups, bitfieldGroups):
	definitions = makeDefSet(iface)

	def genNameProtos ():
		for groupName, values in enumGroups:
			if anyValueDefined(definitions, values):
				yield "const char*\tget%sName\t(int value);" % groupName
			else:
				print "Warning: Empty value set for %s, skipping" % groupName

	def genBitfieldProtos ():
		for groupName, values in bitfieldGroups:
			if anyValueDefined(definitions, values):
				yield "tcu::Format::Bitfield<16>\tget%sStr\t(int value);" % groupName
			else:
				print "Warning: Empty value set for %s, skipping" % groupName

	def genStrImpl ():
		for groupName, values in enumGroups:
			if anyValueDefined(definitions, values):
				yield "inline tcu::Format::Enum<int, 2>\tget%(name)sStr\t(int value)\t{ return tcu::Format::Enum<int, 2>(get%(name)sName, value); }" % {"name": groupName}

		# booleans can be stored in enums or in byte-sized arrays. For clarity add special case
		if anyValueDefined(definitions, ["GL_TRUE", "GL_FALSE"]):
			yield "inline tcu::Format::Enum<int, 1>\tgetBooleanStr\t(deUint8 value)\t{ return tcu::Format::Enum<int, 1>(getBooleanName, (int)value); }"

	return chain(genNameProtos(), genBitfieldProtos(), genStrImpl())

def genEnumStrImpl (groupName, values, definitions):
	if allValuesUndefined(definitions, values):
		return

	yield ""
	yield "const char* get%sName (int value)" % groupName
	yield "{"
	yield "\tswitch (value)"
	yield "\t{"

	def genCases ():
		for value in values:
			if isValueDefined(definitions, value):
				yield "case %s:\treturn \"%s\";" % (value, value)
			else:
				print "Warning: %s not defined, skipping" % value
		yield "default:\treturn DE_NULL;"

	for caseLine in indentLines(genCases()):
		yield "\t\t" + caseLine

	yield "\t}"
	yield "}"

def genBitfieldStrImpl (groupName, values, definitions):
	if allValuesUndefined(definitions, values):
		return

	yield ""
	yield "tcu::Format::Bitfield<16> get%sStr (int value)" % groupName
	yield "{"
	yield "\tstatic const tcu::Format::BitDesc s_desc[] ="
	yield "\t{"

	def genFields ():
		for value in values:
			if isValueDefined(definitions, value):
				yield "tcu::Format::BitDesc(%s,\t\"%s\")," % (value, value)
			else:
				print "Warning: %s not defined, skipping" % value

	for fieldLine in indentLines(genFields()):
		yield "\t\t" + fieldLine

	yield "\t};"
	yield "\treturn tcu::Format::Bitfield<16>(value, &s_desc[0], &s_desc[DE_LENGTH_OF_ARRAY(s_desc)]);"
	yield "}"

def genStrUtilImpls (iface, enumGroups, bitfieldGroups):
	definitions = makeDefSet(iface)

	for groupName, values in enumGroups:
		for line in genEnumStrImpl(groupName, values, definitions):
			yield line
	for groupName, values in bitfieldGroups:
		for line in genBitfieldStrImpl(groupName, values, definitions):
			yield line

def genQueryEnumUtilImpl (groupName, groupQueries, allEnums):
	yield ""
	yield "int get%sQueryNumArgsOut (int pname)" % groupName
	yield "{"
	yield "\tswitch(pname)"
	yield "\t{"

	def genCases ():
		for enumName, enumQueryNumOutputs in groupQueries:
			if enumName in allEnums:
				yield "case %s:\treturn %s;" % (enumName, enumQueryNumOutputs)
			else:
				print "Warning: %s not defined, skipping" % enumName
		yield "default:\treturn 1;"

	for caseLine in indentLines(genCases()):
		yield "\t\t" + caseLine

	yield "\t}"
	yield "}"

def genQueryEnumUtilImpls (iface, queryGroups):
	allEnums = makeDefSet(iface)

	for groupName, groupQueries in queryGroups:
		for line in genQueryEnumUtilImpl(groupName, groupQueries, allEnums):
			yield line

def genSetEnumUtilImpl (groupName, groupQueries, allEnums):
	yield ""
	yield "int get%sNumArgs (int pname)" % groupName
	yield "{"
	yield "\tswitch(pname)"
	yield "\t{"

	def genCases ():
		for enumName, enumQueryNumOutputs in groupQueries:
			if enumName in allEnums:
				yield "case %s:\treturn %s;" % (enumName, enumQueryNumOutputs)
			else:
				print "Warning: %s not defined, skipping" % enumName
		yield "default:\treturn 1;"

	for caseLine in indentLines(genCases()):
		yield "\t\t" + caseLine

	yield "\t}"
	yield "}"

def genSetEnumUtilImpls (iface, queryGroups):
	allEnums = makeDefSet(iface)

	for groupName, groupQueries in queryGroups:
		for line in genSetEnumUtilImpl(groupName, groupQueries, allEnums):
			yield line

def addValuePrefix (groups, prefix):
	return [(groupName, [prefix + value for value in values]) for groupName, values in groups]
