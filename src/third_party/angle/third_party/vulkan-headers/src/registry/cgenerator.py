#!/usr/bin/python3 -i
#
# Copyright (c) 2013-2019 The Khronos Group Inc.
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

import os,re,sys,pdb
from generator import *

# CGeneratorOptions - subclass of GeneratorOptions.
#
# Adds options used by COutputGenerator objects during C language header
# generation.
#
# Additional members
#   prefixText - list of strings to prefix generated header with
#     (usually a copyright statement + calling convention macros).
#   protectFile - True if multiple inclusion protection should be
#     generated (based on the filename) around the entire header.
#   protectFeature - True if #ifndef..#endif protection should be
#     generated around a feature interface in the header file.
#   genFuncPointers - True if function pointer typedefs should be
#     generated
#   protectProto - If conditional protection should be generated
#     around prototype declarations, set to either '#ifdef'
#     to require opt-in (#ifdef protectProtoStr) or '#ifndef'
#     to require opt-out (#ifndef protectProtoStr). Otherwise
#     set to None.
#   protectProtoStr - #ifdef/#ifndef symbol to use around prototype
#     declarations, if protectProto is set
#   apicall - string to use for the function declaration prefix,
#     such as APICALL on Windows.
#   apientry - string to use for the calling convention macro,
#     in typedefs, such as APIENTRY.
#   apientryp - string to use for the calling convention macro
#     in function pointer typedefs, such as APIENTRYP.
#   directory - directory into which to generate include files
#   indentFuncProto - True if prototype declarations should put each
#     parameter on a separate line
#   indentFuncPointer - True if typedefed function pointers should put each
#     parameter on a separate line
#   alignFuncParam - if nonzero and parameters are being put on a
#     separate line, align parameter names at the specified column
class CGeneratorOptions(GeneratorOptions):
    """Represents options during C interface generation for headers"""
    def __init__(self,
                 filename = None,
                 directory = '.',
                 apiname = None,
                 profile = None,
                 versions = '.*',
                 emitversions = '.*',
                 defaultExtensions = None,
                 addExtensions = None,
                 removeExtensions = None,
                 emitExtensions = None,
                 sortProcedure = regSortFeatures,
                 prefixText = "",
                 genFuncPointers = True,
                 protectFile = True,
                 protectFeature = True,
                 protectProto = None,
                 protectProtoStr = None,
                 apicall = '',
                 apientry = '',
                 apientryp = '',
                 indentFuncProto = True,
                 indentFuncPointer = False,
                 alignFuncParam = 0):
        GeneratorOptions.__init__(self, filename, directory, apiname, profile,
                                  versions, emitversions, defaultExtensions,
                                  addExtensions, removeExtensions,
                                  emitExtensions, sortProcedure)
        self.prefixText      = prefixText
        self.genFuncPointers = genFuncPointers
        self.protectFile     = protectFile
        self.protectFeature  = protectFeature
        self.protectProto    = protectProto
        self.protectProtoStr = protectProtoStr
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam  = alignFuncParam

# COutputGenerator - subclass of OutputGenerator.
# Generates C-language API interfaces.
#
# ---- methods ----
# COutputGenerator(errFile, warnFile, diagFile) - args as for
#   OutputGenerator. Defines additional internal state.
# ---- methods overriding base class ----
# beginFile(genOpts)
# endFile()
# beginFeature(interface, emit)
# endFeature()
# genType(typeinfo,name)
# genStruct(typeinfo,name)
# genGroup(groupinfo,name)
# genEnum(enuminfo, name)
# genCmd(cmdinfo)
class COutputGenerator(OutputGenerator):
    """Generate specified API interfaces in a specific style, such as a C header"""
    # This is an ordered list of sections in the header file.
    TYPE_SECTIONS = ['include', 'define', 'basetype', 'handle', 'enum',
                     'group', 'bitmask', 'funcpointer', 'struct']
    ALL_SECTIONS = TYPE_SECTIONS + ['commandPointer', 'command']
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        # Internal state - accumulators for different inner block text
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])
    #
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        # C-specific
        #
        # Multiple inclusion protection & C++ wrappers.
        if (genOpts.protectFile and self.genOpts.filename):
            headerSym = re.sub('\.h', '_h_',
                               os.path.basename(self.genOpts.filename)).upper()
            write('#ifndef', headerSym, file=self.outFile)
            write('#define', headerSym, '1', file=self.outFile)
            self.newline()
        write('#ifdef __cplusplus', file=self.outFile)
        write('extern "C" {', file=self.outFile)
        write('#endif', file=self.outFile)
        self.newline()
        #
        # User-supplied prefix text, if any (list of strings)
        if (genOpts.prefixText):
            for s in genOpts.prefixText:
                write(s, file=self.outFile)
        #
        # Some boilerplate describing what was generated - this
        # will probably be removed later since the extensions
        # pattern may be very long.
        # write('/* Generated C header for:', file=self.outFile)
        # write(' * API:', genOpts.apiname, file=self.outFile)
        # if (genOpts.profile):
        #     write(' * Profile:', genOpts.profile, file=self.outFile)
        # write(' * Versions considered:', genOpts.versions, file=self.outFile)
        # write(' * Versions emitted:', genOpts.emitversions, file=self.outFile)
        # write(' * Default extensions included:', genOpts.defaultExtensions, file=self.outFile)
        # write(' * Additional extensions included:', genOpts.addExtensions, file=self.outFile)
        # write(' * Extensions removed:', genOpts.removeExtensions, file=self.outFile)
        # write(' * Extensions emitted:', genOpts.emitExtensions, file=self.outFile)
        # write(' */', file=self.outFile)
    def endFile(self):
        # C-specific
        # Finish C++ wrapper and multiple inclusion protection
        self.newline()
        write('#ifdef __cplusplus', file=self.outFile)
        write('}', file=self.outFile)
        write('#endif', file=self.outFile)
        if (self.genOpts.protectFile and self.genOpts.filename):
            self.newline()
            write('#endif', file=self.outFile)
        # Finish processing in superclass
        OutputGenerator.endFile(self)
    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        # C-specific
        # Accumulate includes, defines, types, enums, function pointer typedefs,
        # end function prototypes separately for this feature. They're only
        # printed in endFeature().
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])
    def endFeature(self):
        # C-specific
        # Actually write the interface to the output file.
        if (self.emit):
            self.newline()
            if (self.genOpts.protectFeature):
                write('#ifndef', self.featureName, file=self.outFile)
            # If type declarations are needed by other features based on
            # this one, it may be necessary to suppress the ExtraProtect,
            # or move it below the 'for section...' loop.
            if (self.featureExtraProtect != None):
                write('#ifdef', self.featureExtraProtect, file=self.outFile)
            write('#define', self.featureName, '1', file=self.outFile)
            for section in self.TYPE_SECTIONS:
                contents = self.sections[section]
                if contents:
                    write('\n'.join(contents), file=self.outFile)
                    self.newline()
            if (self.genOpts.genFuncPointers and self.sections['commandPointer']):
                write('\n'.join(self.sections['commandPointer']), file=self.outFile)
                self.newline()
            if (self.sections['command']):
                if (self.genOpts.protectProto):
                    write(self.genOpts.protectProto,
                          self.genOpts.protectProtoStr, file=self.outFile)
                write('\n'.join(self.sections['command']), end='', file=self.outFile)
                if (self.genOpts.protectProto):
                    write('#endif', file=self.outFile)
                else:
                    self.newline()
            if (self.featureExtraProtect != None):
                write('#endif /*', self.featureExtraProtect, '*/', file=self.outFile)
            if (self.genOpts.protectFeature):
                write('#endif /*', self.featureName, '*/', file=self.outFile)
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    # Append a definition to the specified section
    def appendSection(self, section, text):
        # self.sections[section].append('SECTION: ' + section + '\n')
        self.sections[section].append(text)
        # self.logMsg('diag', 'appendSection(section =', section, 'text =', text)
    #
    # Type generation
    def genType(self, typeinfo, name, alias):
        OutputGenerator.genType(self, typeinfo, name, alias)
        typeElem = typeinfo.elem

        # Determine the category of the type, and the type section to add
        # its definition to.
        # 'funcpointer' is added to the 'struct' section as a workaround for
        # internal issue #877, since structures and function pointer types
        # can have cross-dependencies.
        category = typeElem.get('category')
        if category == 'funcpointer':
            section = 'struct'
        else:
            section = category

        if category == 'struct' or category == 'union':
            # If the type is a struct type, generate it using the
            # special-purpose generator.
            self.genStruct(typeinfo, name, alias)
        else:
            if alias:
                # If the type is an alias, just emit a typedef declaration
                body = 'typedef ' + alias + ' ' + name + ';\n'
            else:
                # Replace <apientry /> tags with an APIENTRY-style string
                # (from self.genOpts). Copy other text through unchanged.
                # If the resulting text is an empty string, don't emit it.
                body = noneStr(typeElem.text)
                for elem in typeElem:
                    if (elem.tag == 'apientry'):
                        body += self.genOpts.apientry + noneStr(elem.tail)
                    else:
                        body += noneStr(elem.text) + noneStr(elem.tail)

            if body:
                # Add extra newline after multi-line entries.
                if '\n' in body[0:-1]:
                    body += '\n'
                self.appendSection(section, body)
    #
    # Struct (e.g. C "struct" type) generation.
    # This is a special case of the <type> tag where the contents are
    # interpreted as a set of <member> tags instead of freeform C
    # C type declarations. The <member> tags are just like <param>
    # tags - they are a declaration of a struct or union member.
    # Only simple member declarations are supported (no nested
    # structs etc.)
    # If alias != None, then this struct aliases another; just
    #   generate a typedef of that alias.
    def genStruct(self, typeinfo, typeName, alias):
        OutputGenerator.genStruct(self, typeinfo, typeName, alias)

        typeElem = typeinfo.elem

        if alias:
            body = 'typedef ' + alias + ' ' + typeName + ';\n'
        else:
            body = 'typedef ' + typeElem.get('category') + ' ' + typeName + ' {\n'

            targetLen = 0;
            for member in typeElem.findall('.//member'):
                targetLen = max(targetLen, self.getCParamTypeLength(member))
            for member in typeElem.findall('.//member'):
                body += self.makeCParamDecl(member, targetLen + 4)
                body += ';\n'
            body += '} ' + typeName + ';\n'

        self.appendSection('struct', body)
    #
    # Group (e.g. C "enum" type) generation.
    # These are concatenated together with other types.
    # If alias != None, it is the name of another group type
    #   which aliases this type; just generate that alias.
    def genGroup(self, groupinfo, groupName, alias = None):
        OutputGenerator.genGroup(self, groupinfo, groupName, alias)
        groupElem = groupinfo.elem

        if alias:
            # If the group name is aliased, just emit a typedef declaration
            # for the alias.
            body = 'typedef ' + alias + ' ' + groupName + ';\n'
        else:
            self.logMsg('diag', 'CGenerator.genGroup group =', groupName, 'alias =', alias)

            # Otherwise, emit an actual enumerated type declaration
            expandName = re.sub(r'([0-9a-z_])([A-Z0-9])',r'\1_\2',groupName).upper()

            expandPrefix = expandName
            expandSuffix = ''
            expandSuffixMatch = re.search(r'[A-Z][A-Z]+$',groupName)
            if expandSuffixMatch:
                expandSuffix = '_' + expandSuffixMatch.group()
                # Strip off the suffix from the prefix
                expandPrefix = expandName.rsplit(expandSuffix, 1)[0]

            # Prefix
            body = "\ntypedef enum " + groupName + " {\n"

            # @@ Should use the type="bitmask" attribute instead
            isEnum = ('FLAG_BITS' not in expandPrefix)

            # Get a list of nested 'enum' tags.
            enums = groupElem.findall('enum')

            # Check for and report duplicates, and return a list with them
            # removed.
            enums = self.checkDuplicateEnums(enums)

            # Loop over the nested 'enum' tags. Keep track of the minimum and
            # maximum numeric values, if they can be determined; but only for
            # core API enumerants, not extension enumerants. This is inferred
            # by looking for 'extends' attributes.
            minName = None

            # Accumulate non-numeric enumerant values separately and append
            # them following the numeric values, to allow for aliases.
            # NOTE: this doesn't do a topological sort yet, so aliases of
            # aliases can still get in the wrong order.
            aliasText = ""

            for elem in enums:
                # Convert the value to an integer and use that to track min/max.
                (numVal,strVal) = self.enumToValue(elem, True)
                name = elem.get('name')

                # Extension enumerants are only included if they are required
                if self.isEnumRequired(elem):
                    decl = "    " + name + " = " + strVal + ",\n"
                    if numVal != None:
                        body += decl
                    else:
                        aliasText += decl

                # Don't track min/max for non-numbers (numVal == None)
                if isEnum and numVal != None and elem.get('extends') is None:
                    if minName == None:
                        minName = maxName = name
                        minValue = maxValue = numVal
                    elif numVal < minValue:
                        minName = name
                        minValue = numVal
                    elif numVal > maxValue:
                        maxName = name
                        maxValue = numVal

            # Now append the non-numeric enumerant values
            body += aliasText

            # Generate min/max value tokens and a range-padding enum. Need some
            # additional padding to generate correct names...
            if isEnum:
                body += "    " + expandPrefix + "_BEGIN_RANGE" + expandSuffix + " = " + minName + ",\n"
                body += "    " + expandPrefix + "_END_RANGE" + expandSuffix + " = " + maxName + ",\n"
                body += "    " + expandPrefix + "_RANGE_SIZE" + expandSuffix + " = (" + maxName + " - " + minName + " + 1),\n"

            body += "    " + expandPrefix + "_MAX_ENUM" + expandSuffix + " = 0x7FFFFFFF\n"

            # Postfix
            body += "} " + groupName + ";"

        # After either enumerated type or alias paths, add the declaration
        # to the appropriate section for the group being defined.
        if groupElem.get('type') == 'bitmask':
            section = 'bitmask'
        else:
            section = 'group'
        self.appendSection(section, body)

    # Enumerant generation
    # <enum> tags may specify their values in several ways, but are usually
    # just integers.
    def genEnum(self, enuminfo, name, alias):
        OutputGenerator.genEnum(self, enuminfo, name, alias)
        (numVal,strVal) = self.enumToValue(enuminfo.elem, False)
        body = '#define ' + name.ljust(33) + ' ' + strVal
        self.appendSection('enum', body)

    #
    # Command generation
    def genCmd(self, cmdinfo, name, alias):
        OutputGenerator.genCmd(self, cmdinfo, name, alias)

        # if alias:
        #     prefix = '// ' + name + ' is an alias of command ' + alias + '\n'
        # else:
        #     prefix = ''

        prefix = ''
        decls = self.makeCDecls(cmdinfo.elem)
        self.appendSection('command', prefix + decls[0] + '\n')
        if (self.genOpts.genFuncPointers):
            self.appendSection('commandPointer', decls[1])
