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
import itertools
import operator

import genutil

from genutil import Scalar, Vec2, Vec3, Vec4, Uint, UVec2, UVec3, UVec4, CaseGroup


# Templates

ARTIHMETIC_CASE_TEMPLATE = """
case ${{NAME}}
	version 310 es
	require extension { "GL_EXT_shader_implicit_conversions" } in { vertex, fragment }
	values
	{
		${{VALUES}}
	}

	both ""
		#version 310 es
		precision highp float;
		${DECLARATIONS}

		void main()
		{
			${SETUP}
			out0 = ${{EXPR}};
			${OUTPUT}
		}
	""
end
""".strip()

FUNCTIONS_CASE_TEMPLATE = """
case ${{NAME}}
	version 310 es
	require extension { "GL_EXT_shader_implicit_conversions" } in { vertex, fragment }
	values
	{
		${{VALUES}}
	}

	both ""
		#version 310 es
		precision highp float;
		${DECLARATIONS}

		${{OUTTYPE}} func (${{OUTTYPE}} a)
		{
			return a * ${{OUTTYPE}}(2);
		}

		void main()
		{
			${SETUP}
			out0 = func(in0);
			${OUTPUT}
		}
	""
end
""".strip()

ARRAY_CASE_TEMPLATE = """
case ${{NAME}}
	version 310 es
	require extension { "GL_EXT_shader_implicit_conversions" } in { vertex, fragment }
	values
	{
		${{VALUES}}
	}

	both ""
		#version 310 es
		precision highp float;
		${DECLARATIONS}

		void main()
		{
			${SETUP}
			${{ARRAYTYPE}}[] x = ${{ARRAYTYPE}}[] (${{ARRAYVALUES}});
			out0 = ${{EXPR}};
			${OUTPUT}
		}
	""
end
""".strip()

STRUCT_CASE_TEMPLATE = """
case ${{NAME}}
	version 310 es
	require extension { "GL_EXT_shader_implicit_conversions" } in { vertex, fragment }
	values
	{
		${{VALUES}}
	}

	both ""
		#version 310 es
		precision highp float;
		${DECLARATIONS}

		void main()
		{
			${SETUP}
			struct {
				${{OUTTYPE}} val;
			} x;

			x.val = ${{STRUCTVALUE}};

			out0 = ${{EXPR}};
			${OUTPUT}
		}
	""
end
""".strip()

INVALID_CASE_TEMPLATE = """
case ${{NAME}}
	expect compile_fail
	version 310 es
	require extension { "GL_EXT_shader_implicit_conversions" } in { vertex, fragment }
	values
	{
		${{VALUES}}
	}

	both ""
		#version 310 es
		precision highp float;
		${DECLARATIONS}

		void main()
		{
			${SETUP}
			out0 = in0 + ${{OPERAND}};
			${OUTPUT}
		}
	""
end
""".strip()

INVALID_ARRAY_CASE_TEMPLATE = """
case ${{NAME}}
	expect compile_fail
	version 310 es
	require extension { "GL_EXT_shader_implicit_conversions" } in { vertex, fragment }
	values {}

	both ""
		#version 310 es
		precision highp float;
		${DECLARATIONS}

		void main()
		{
			${SETUP}
			${{EXPR}}
			${OUTPUT}
		}
	""
end
""".strip()

INVALID_STRUCT_CASE_TEMPLATE = """
case ${{NAME}}
	expect compile_fail
	version 310 es
	require extension { "GL_EXT_shader_implicit_conversions" } in { vertex, fragment }
	values {}

	both ""
		#version 310 es
		precision highp float;
		${DECLARATIONS}

		void main()
		{
			${SETUP}
			struct { ${{INTYPE}} value; } a;
			struct { ${{OUTTYPE}} value; } b;
			a = ${{INVALUE}};
			b = a;
			${OUTPUT}
		}
	""
end
""".strip()


# Input values

IN_ISCALAR = [  2,  1,  1,  3,  5 ]
IN_USCALAR = [  1,  3,  4,  7, 11 ]

IN_IVECTOR = [
	( 1,  2,  3,  4),
	( 2,  1,  2,  6),
	( 3,  7,  2,  5),
]

IN_UVECTOR = [
	( 2,  3,  5,  8),
	( 4,  6,  2,  9),
	( 1, 13,  7,  4),
]

IN_VALUES = {
	"int":		[Scalar(x)								for x in IN_ISCALAR],
	"uint":		[Scalar(x)								for x in IN_USCALAR],
	"ivec2":	[Vec2(x[0], x[1])						for x in IN_IVECTOR],
	"uvec2":	[Vec2(x[0], x[1])						for x in IN_UVECTOR],
	"ivec3":	[Vec3(x[0], x[1], x[2])					for x in IN_IVECTOR],
	"uvec3":	[Vec3(x[0], x[1], x[2])					for x in IN_UVECTOR],
	"ivec4":	[Vec4(x[0], x[1], x[2], x[3])			for x in IN_IVECTOR],
	"uvec4":	[Vec4(x[0], x[1], x[2], x[3])			for x in IN_UVECTOR],
	"float":	[Scalar(x).toFloat()					for x in IN_ISCALAR],
	"vec2":		[Vec2(x[0], x[1]).toFloat()				for x in IN_IVECTOR],
	"vec3":		[Vec3(x[0], x[1], x[2]).toFloat()		for x in IN_IVECTOR],
	"vec4":		[Vec4(x[0], x[1], x[2], x[3]).toFloat()	for x in IN_IVECTOR],
}

VALID_CONVERSIONS = {
	"int":		["float", "uint"],
	"uint":		["float"],
	"ivec2":	["uvec2", "vec2"],
	"uvec2":	["vec2"],
	"ivec3":	["uvec3", "vec3"],
	"uvec3":	["vec3"],
	"ivec4":	["uvec4", "vec4"],
	"uvec4":	["vec4"]
}

SCALAR_TO_VECTOR_CONVERSIONS = {
	"int":		["vec2", "vec3", "vec4", "uvec2", "uvec3", "uvec4"],
	"uint":		["vec2", "vec3", "vec4"]
}

VALID_ASSIGNMENTS = {
	"int":		["ivec2", "ivec3", "ivec4"],
	"uint":		["uvec2", "uvec3", "uvec4"],
	"ivec2":	["int", "float"],
	"ivec3":	["int", "float"],
	"ivec4":	["int", "float"],
	"uvec2":	["uint", "float"],
	"uvec3":	["uint", "float"],
	"uvec4":	["uint", "float"],
	"float":	["vec2", "vec3", "vec4"],
	"vec2":		["float"],
	"vec3":		["float"],
	"vec4":		["float"]
}

IN_TYPE_ORDER = [
	"int",	 "uint",
	"ivec2", "uvec2", "ivec3",
	"uvec3", "ivec4", "uvec4",

	"float",
	"vec2",  "vec3",  "vec4"
]

def isScalarTypeName (name):
	return name in ["float", "int", "uint"]

def isVec2TypeName (name):
	return name in ["vec2", "ivec2", "uvec2"]

def isVec3TypeName (name):
	return name in ["vec3", "ivec3", "uvec3"]

def isVec4TypeName (name):
	return name in ["vec4", "ivec4", "uvec4"]

# Utilities

def scalarToVector(a, b):
	if isinstance(a, Scalar) and isinstance(b, Vec2):
		a = a.toVec2()
	elif isinstance(a, Scalar) and isinstance(b, Vec3):
		a = a.toVec3()
	elif isinstance(a, Scalar) and isinstance(b, Vec4):
		a = a.toVec4()
	return a

def isUintTypeName (type_name):
	return type_name in ["uint", "uvec2", "uvec3", "uvec4"]

def convLiteral (type, value):
	if isUintTypeName(type):
		return int(value)
	else:
		return value

def valueToStr(value_type, value):
	if isinstance(value, Scalar):
		return str(value)
	else:
		assert isinstance(value, genutil.Vec)
		out = value_type + "("
		out += ", ".join([str(convLiteral(value_type, x)) for x in value.getScalars()])
		out += ")"
		return out


def valuesToStr(prefix, value_type, values):
	def gen_value_strs(value_list, value_type):
		for value in value_list:
			yield valueToStr(value_type, value)
	return "%s = [ %s ];" % (prefix, " | ".join(gen_value_strs(values, value_type)))


# Test cases

class ArithmeticCase(genutil.ShaderCase):
	def __init__(self, name, op, in_type, out_type, reverse=False):
		self.op_func = {
			"+":	operator.add,
			"-":	operator.sub,
			"*":	operator.mul,
			"/":	operator.div,
		}
		self.name		= name
		self.op			= op
		self.in_type	= in_type
		self.out_type	= out_type
		self.reverse	= reverse

	def __str__(self):
		params = {
			"NAME":		self.name,
			"EXPR":		self.get_expr(),
			"VALUES":	self.gen_values(),
		}
		return genutil.fillTemplate(ARTIHMETIC_CASE_TEMPLATE, params)

	def apply(self, a, b):
		assert(self.op in self.op_func)
		a = scalarToVector(a, b)

		if self.reverse:
			b, a = a, b

		return self.op_func[self.op](a, b)

	def get_expr(self):
		expr = ["in0", self.op, str(self.get_operand())]

		if self.reverse:
			expr.reverse()

		return " ".join(expr)

	def get_operand(self):
		operands = {
			"float":	Scalar(2.0),
			"vec2":		Vec2(1.0, 2.0),
			"vec3":		Vec3(1.0, 2.0, 3.0),
			"vec4":		Vec4(1.0, 2.0, 3.0, 4.0),
			"uint":		Uint(2),
			"uvec2":	UVec2(1, 2),
			"uvec3":	UVec3(1, 2, 3),
			"uvec4":	UVec4(1, 2, 3, 4),
		}
		assert self.out_type in operands
		return operands[self.out_type]

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]

		y			= self.get_operand()
		out_values	= [self.apply(x, y) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output %s out0" % (self.out_type), self.out_type, out_values))

		return "\n".join(out)


class ComparisonsCase(ArithmeticCase):
	def __init__(self, name, op, in_type, out_type, reverse=False):
		super(ComparisonsCase, self).__init__(name, op, in_type, out_type, reverse)

		self.op_func = {
			"==":	operator.eq,
			"!=":	operator.ne,
			"<":	operator.lt,
			">":	operator.gt,
			"<=":	operator.le,
			">=":	operator.ge,
		}

	def apply(self, a, b):
		assert(self.op in self.op_func)

		if isinstance(a, Scalar) and isinstance(b, Scalar):
			a, b = float(a), float(b)

		if self.reverse:
			b, a = a, b

		return Scalar(self.op_func[self.op](a, b))

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]

		y			= self.get_operand()
		out_values	= [self.apply(x, y) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output bool out0", "bool", out_values))

		return "\n".join(out)


class ParenthesizedCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type, reverse=False, input_in_parens=False):
		self.name				= name
		self.in_type			= in_type
		self.out_type			= out_type
		self.reverse			= reverse
		self.input_in_parens	= input_in_parens

	def __str__(self):
		params = {
			"NAME":		self.name,
			"EXPR":		self.get_expr(),
			"VALUES":	self.gen_values(),
		}
		return genutil.fillTemplate(ARTIHMETIC_CASE_TEMPLATE, params)

	def apply(self, a):
		b, c	= self.get_operand(0), self.get_operand(1)
		a		= scalarToVector(a, b)

		if self.input_in_parens:
			return b*(a+c)
		else:
			return a*(b+c)

	def get_expr(self):
		def make_paren_expr():
			out = [
				"in0" if self.input_in_parens else self.get_operand(0),
				"+",
				self.get_operand(1)
			]
			return "(%s)" % (" ".join([str(x) for x in out]))

		expr = [
			"in0" if not self.input_in_parens else self.get_operand(0),
			"*",
			make_paren_expr()
		]

		if self.reverse:
			expr.reverse()

		return " ".join([str(x) for x in expr])

	def get_operand(self, ndx=0):
		return IN_VALUES[self.out_type][ndx]

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]

		out_values	= [self.apply(x) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output %s out0" % (self.out_type), self.out_type, out_values))

		return "\n".join(out)


class FunctionsCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type):
		self.name		= name
		self.in_type	= in_type
		self.out_type	= out_type

	def __str__(self):
		params = {
			"NAME":		self.name,
			"OUTTYPE":	self.out_type,
			"VALUES":	self.gen_values(),
		}
		return genutil.fillTemplate(FUNCTIONS_CASE_TEMPLATE, params)

	def apply(self, a):
		if isUintTypeName(self.out_type):
			return a.toUint() * Uint(2)
		else:
			return a.toFloat() * Scalar(2.0)

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]
		out_values	= [self.apply(x) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output %s out0" % (self.out_type), self.out_type, out_values))

		return "\n".join(out)


class ArrayCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type, reverse=False):
		self.name		= name
		self.in_type	= in_type
		self.out_type	= out_type
		self.reverse	= reverse

	def __str__(self):
		params = {
			"NAME":			self.name,
			"VALUES":		self.gen_values(),
			"ARRAYTYPE":	self.out_type,
			"ARRAYVALUES":	self.gen_array_values(),
			"EXPR":			self.get_expr(),
		}
		return genutil.fillTemplate(ARRAY_CASE_TEMPLATE, params)

	def apply(self, a):
		b = IN_VALUES[self.out_type][1]
		a = scalarToVector(a, b)

		return a + b

	def get_expr(self):
		if not self.reverse:
			return "in0 + x[1]"
		else:
			return "x[1] + in0"

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]
		out_values	= [self.apply(x) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output %s out0" % (self.out_type), self.out_type, out_values))

		return "\n".join(out)

	def gen_array_values(self):
		out = [valueToStr(self.out_type, x) for x in IN_VALUES[self.out_type]]
		return ", ".join(out)


class ArrayUnpackCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type):
		self.name		= name
		self.in_type	= in_type
		self.out_type	= out_type

	def __str__(self):
		params = {
			"NAME":			self.name,
			"VALUES":		self.gen_values(),
			"ARRAYTYPE":	"float",
			"ARRAYVALUES":	self.gen_array_values(),
			"EXPR":			self.get_expr(),
		}
		return genutil.fillTemplate(ARRAY_CASE_TEMPLATE, params)

	def apply(self, a):
		if isinstance(a, Scalar) and isVec2TypeName(self.out_type):
			a = a.toVec2()
		elif isinstance(a, Scalar) and isVec3TypeName(self.out_type):
			a = a.toVec3()
		elif isinstance(a, Scalar) and isVec4TypeName(self.out_type):
			a = a.toVec4()

		b = IN_VALUES["float"]

		out = [Scalar(x)+y for x, y in zip(a.getScalars(), b)]

		if self.out_type == "float":
			return out[0].toFloat()
		elif self.out_type == "uint":
			return out[0].toUint()
		elif self.out_type == "vec2":
			return Vec2(out[0], out[1]).toFloat()
		elif self.out_type == "uvec2":
			return Vec2(out[0], out[1]).toUint()
		elif self.out_type == "vec3":
			return Vec3(out[0], out[1], out[2]).toFloat()
		elif self.out_type == "uvec3":
			return Vec3(out[0], out[1], out[2]).toUint()
		elif self.out_type == "vec4":
			return Vec4(out[0], out[1], out[2], out[3]).toFloat()
		elif self.out_type == "uvec4":
			return Vec4(out[0], out[1], out[2], out[3]).toUint()

	def get_expr(self):
		def num_scalars(typename):
			return IN_VALUES[typename][0].getNumScalars()

		def gen_sums():
			in_scalars	= num_scalars(self.in_type)
			out_scalars	= num_scalars(self.out_type)

			for ndx in range(out_scalars):
				if in_scalars > 1:
					yield "in0[%i] + x[%i]" % (ndx, ndx)
				else:
					yield "in0 + x[%i]" % (ndx)

		return "%s(%s)" % (self.out_type, ", ".join(gen_sums()))

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]
		out_values	= [self.apply(x) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output %s out0" % (self.out_type), self.out_type, out_values))

		return "\n".join(out)

	def gen_array_values(self):
		out = [valueToStr(self.out_type, x) for x in IN_VALUES["float"]]
		return ", ".join(out)


class StructCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type, reverse=False):
		self.name		= name
		self.in_type	= in_type
		self.out_type	= out_type
		self.reverse	= reverse

	def __str__(self):
		params = {
			"NAME":			self.name,
			"VALUES":		self.gen_values(),
			"OUTTYPE":		self.out_type,
			"STRUCTVALUE":	self.get_struct_value(),
			"EXPR":			self.get_expr(),
		}
		return genutil.fillTemplate(STRUCT_CASE_TEMPLATE, params)

	def apply(self, a):
		if isinstance(a, Scalar) and isVec2TypeName(self.out_type):
			a = a.toVec2()
		elif isinstance(a, Scalar) and isVec3TypeName(self.out_type):
			a = a.toVec3()
		elif isinstance(a, Scalar) and isVec4TypeName(self.out_type):
			a = a.toVec4()

		b = IN_VALUES[self.out_type][0]

		return a + b

	def get_expr(self):
		if not self.reverse:
			return "in0 + x.val"
		else:
			return "x.val + in0"

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]
		out_values	= [self.apply(x) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output %s out0" % (self.out_type), self.out_type, out_values))

		return "\n".join(out)

	def get_struct_value(self):
		return valueToStr(self.out_type, IN_VALUES[self.out_type][0])


class InvalidCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type):
		self.name		= name
		self.in_type	= in_type
		self.out_type	= out_type

	def __str__(self):
		params = {
			"NAME":		self.name,
			"OPERAND":	str(self.get_operand()),
			"VALUES":	self.gen_values(),
		}
		return genutil.fillTemplate(INVALID_CASE_TEMPLATE, params)

	def apply(self, a, b):
		return b

	def get_operand(self):
		return IN_VALUES[self.out_type][0]

	def gen_values(self):
		in_values	= IN_VALUES[self.in_type]

		y			= self.get_operand()
		out_values	= [self.apply(x, y) for x in in_values]

		out = []
		out.append(valuesToStr("input %s in0" % (self.in_type), self.in_type, in_values))
		out.append(valuesToStr("output %s out0" % (self.out_type), self.out_type, out_values))

		return "\n".join(out)


class InvalidArrayCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type):
		self.name		= name
		self.in_type	= in_type
		self.out_type	= out_type

	def __str__(self):
		params = {
			"NAME":	self.name,
			"EXPR":	self.gen_expr(),
		}
		return genutil.fillTemplate(INVALID_ARRAY_CASE_TEMPLATE, params)

	def gen_expr(self):
		in_values = [valueToStr(self.out_type, x) for x in IN_VALUES[self.in_type]]

		out = "%s a[] = %s[] (%s);" % (self.out_type, self.in_type, ", ".join(in_values))

		return out


class InvalidStructCase(genutil.ShaderCase):
	def __init__(self, name, in_type, out_type):
		self.name		= name
		self.in_type	= in_type
		self.out_type	= out_type

	def __str__(self):
		params = {
			"NAME":		self.name,
			"INTYPE":	self.in_type,
			"OUTTYPE":	self.out_type,
			"INVALUE":	self.get_value(),
		}
		return genutil.fillTemplate(INVALID_STRUCT_CASE_TEMPLATE, params)

	def get_value(self):
		return valueToStr(self.in_type, IN_VALUES[self.in_type][0])


# Case file generation

def genConversionPairs(order=IN_TYPE_ORDER, scalar_to_vector=True, additional={}):
	def gen_order(conversions):
		key_set = set(conversions.iterkeys())
		for typename in order:
			if typename in key_set:
				yield typename
	conversions = {}

	for in_type in VALID_CONVERSIONS:
		conversions[in_type] = [] + VALID_CONVERSIONS[in_type]
		if in_type in SCALAR_TO_VECTOR_CONVERSIONS and scalar_to_vector:
			conversions[in_type] += SCALAR_TO_VECTOR_CONVERSIONS[in_type]

	for key in additional.iterkeys():
			value = conversions.get(key, [])
			conversions[key] = value + additional[key]

	for in_type in gen_order(conversions):
		for out_type in conversions[in_type]:
			yield (in_type, out_type)


def genInvalidConversions():
	types = IN_TYPE_ORDER
	valid_pairs = set(genConversionPairs(additional=VALID_ASSIGNMENTS))

	for pair in itertools.permutations(types, 2):
		if pair not in valid_pairs:
			yield pair


def genArithmeticCases(reverse=False):
	op_names = [
		("add", "Addition",			"+"),
		("sub", "Subtraction",		"-"),
		("mul", "Multiplication",	"*"),
		("div", "Division",			"/")
	]

	for name, desc, op in op_names:
		casegroup = CaseGroup(name, desc, [])
		for in_type, out_type in genConversionPairs():
			if op == "-" and isUintTypeName(out_type):
				continue # Can't handle at the moment
			name = in_type + "_to_" + out_type
			casegroup.children.append(ArithmeticCase(name, op, in_type, out_type, reverse))
		yield casegroup


def genComparisonCases(reverse=False):
	op_names = [
		("equal",				"Equal",					"=="),
		("not_equal",			"Not equal",				"!="),
		("less",				"Less than",				"<"),
		("greater",				"Greater than",				">"),
		("less_or_equal",		"Less than or equal",		"<="),
		("greater_or_equal",	"Greater than or equal",	">="),
	]

	for name, desc, op in op_names:
		casegroup	= CaseGroup(name, desc, [])
		type_order	= IN_TYPE_ORDER if name in ["equal", "not_equal"] else ["int", "uint"]

		for in_type, out_type in genConversionPairs(order=type_order, scalar_to_vector=False):
			name = in_type + "_to_" + out_type
			casegroup.children.append(ComparisonsCase(name, op, in_type, out_type, reverse))
		yield casegroup


def genParenthesizedCases():
	for reverse in [True, False]:
		if reverse:
			name = "paren_expr_before_literal"
			desc = "Parenthesized expression before literal"
		else:
			name = "literal_before_paren_expr"
			desc = "Literal before parenthesized expression"
		reversegroup = CaseGroup(name, desc, [])

		for input_in_parens in [True, False]:
			if input_in_parens:
				name = "input_in_parens"
				desc = "Input variable in parenthesized expression"
			else:
				name = "input_outside_parens"
				desc = "Input variable outside parenthesized expression"
			casegroup = CaseGroup(name, desc, [])

			for in_type, out_type in genConversionPairs():
				name = in_type + "_to_" + out_type
				casegroup.children.append(
					ParenthesizedCase(name, in_type, out_type, reverse, input_in_parens)
				)
			reversegroup.children.append(casegroup)
		yield reversegroup


def genArrayCases(reverse=False):
	for in_type, out_type in genConversionPairs():
		name = in_type + "_to_" + out_type
		yield ArrayCase(name, in_type, out_type, reverse)


def genArrayUnpackCases(reverse=False):
	for in_type, out_type in genConversionPairs():
		name = in_type + "_to_" + out_type
		yield ArrayUnpackCase(name, in_type, out_type)


def genFunctionsCases():
	for in_type, out_type in genConversionPairs(scalar_to_vector=False):
		name = in_type + "_to_" + out_type
		yield FunctionsCase(name, in_type, out_type)


def genStructCases(reverse=False):
	for in_type, out_type in genConversionPairs():
		name = in_type + "_to_" + out_type
		yield StructCase(name, in_type, out_type, reverse)


def genInvalidCases(reverse=False):
	for in_type, out_type in genInvalidConversions():
		name = in_type + "_to_" + out_type
		yield InvalidCase(name, in_type, out_type)


def genInvalidArrayCases():
	for in_type, out_type in genConversionPairs(scalar_to_vector=False):
		name = in_type + "_to_" + out_type
		yield InvalidArrayCase(name, in_type, out_type)


def genInvalidStructCases():
	for in_type, out_type in genConversionPairs(scalar_to_vector=False):
		name = in_type + "_to_" + out_type
		yield InvalidStructCase(name, in_type, out_type)


def genAllCases():
	yield CaseGroup(
		"arithmetic", "Arithmetic operations",
		[
			CaseGroup("input_before_literal", "Input before literal",
					  genArithmeticCases(reverse=False)),
			CaseGroup("literal_before_input", "Literal before input",
					  genArithmeticCases(reverse=True)),
		]
	)

	yield CaseGroup(
		"comparisons", "Comparisons",
		[
			CaseGroup("input_before_literal", "Input before literal",
					  genComparisonCases(reverse=False)),
			CaseGroup("literal_before_input", "Literal before input",
					  genComparisonCases(reverse=True)),
		]
	)

	yield CaseGroup(
		"array_subscripts", "Array subscripts",
		[
			CaseGroup("input_before_subscript", "Input before subscript",
					  genArrayCases(reverse=False)),
			CaseGroup("subscript_before_input", "Subscript before input",
					  genArrayCases(reverse=True)),
		#	CaseGroup("unpack", "Unpack array and repack as value",
		#			  genArrayUnpackCases()),
		]
	)

	yield CaseGroup("functions", "Function calls",
					genFunctionsCases())

	yield CaseGroup("struct_fields", "Struct field selectors",
		[
			CaseGroup("input_before_field", "Input before field",
					  genStructCases(reverse=False)),
			CaseGroup("field_before_input", "Field before input",
					  genStructCases(reverse=True)),
		]
	)

	yield CaseGroup("parenthesized_expressions", "Parenthesized expressions",
					genParenthesizedCases())

	yield CaseGroup(
		"invalid", "Invalid conversions",
		[
			CaseGroup("variables", "Single variables",
					  genInvalidCases()),
			CaseGroup("arrays", "Arrays",
					  genInvalidArrayCases()),
			CaseGroup("structs", "Structs",
					  genInvalidStructCases()),
		]
	)


if __name__ == "__main__":
	print("Generating shader case files.")
	genutil.writeAllCases("implicit_conversions.test", genAllCases())
