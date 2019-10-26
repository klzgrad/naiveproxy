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

from __future__ import unicode_literals
import io,os,re,sys,pdb

def write( *args, **kwargs ):
    file = kwargs.pop('file',sys.stdout)
    end = kwargs.pop('end','\n')
    file.write(' '.join([str(arg) for arg in args]))
    file.write(end)

# noneStr - returns string argument, or "" if argument is None.
# Used in converting etree Elements into text.
#   str - string to convert
def noneStr(str):
    if (str):
        return str
    else:
        return ""

# enquote - returns string argument with surrounding quotes,
#   for serialization into Python code.
def enquote(str):
    if (str):
        return "'" + str + "'"
    else:
        return None

# apiName - returns True if name is a Vulkan name (vk/Vk/VK prefix, or a
# function pointer type), False otherwise.
def apiName(str):
    return str[0:2].lower() == 'vk' or str[0:3] == 'PFN'

# Primary sort key for regSortFeatures.
# Sorts by category of the feature name string:
#   Core API features (those defined with a <feature> tag)
#   ARB/KHR/OES (Khronos extensions)
#   other       (EXT/vendor extensions)
# This will need changing for Vulkan!
def regSortCategoryKey(feature):
    if (feature.elem.tag == 'feature'):
        return 0
    elif (feature.category == 'ARB' or
          feature.category == 'KHR' or
          feature.category == 'OES'):
        return 1
    else:
        return 2

# Secondary sort key for regSortFeatures.
# Sorts by extension name.
def regSortNameKey(feature):
    return feature.name

# Second sort key for regSortFeatures.
# Sorts by feature version. <extension> elements all have version number "0"
def regSortFeatureVersionKey(feature):
    return float(feature.versionNumber)

# Tertiary sort key for regSortFeatures.
# Sorts by extension number. <feature> elements all have extension number 0.
def regSortExtensionNumberKey(feature):
    return int(feature.number)

# regSortFeatures - default sort procedure for features.
# Sorts by primary key of feature category ('feature' or 'extension')
#   then by version number (for features)
#   then by extension number (for extensions)
def regSortFeatures(featureList):
    featureList.sort(key = regSortExtensionNumberKey)
    featureList.sort(key = regSortFeatureVersionKey)
    featureList.sort(key = regSortCategoryKey)

# GeneratorOptions - base class for options used during header production
# These options are target language independent, and used by
# Registry.apiGen() and by base OutputGenerator objects.
#
# Members
#   filename - basename of file to generate, or None to write to stdout.
#   directory - directory in which to generate filename
#   apiname - string matching <api> 'apiname' attribute, e.g. 'gl'.
#   profile - string specifying API profile , e.g. 'core', or None.
#   versions - regex matching API versions to process interfaces for.
#     Normally '.*' or '[0-9]\.[0-9]' to match all defined versions.
#   emitversions - regex matching API versions to actually emit
#    interfaces for (though all requested versions are considered
#    when deciding which interfaces to generate). For GL 4.3 glext.h,
#    this might be '1\.[2-5]|[2-4]\.[0-9]'.
#   defaultExtensions - If not None, a string which must in its
#     entirety match the pattern in the "supported" attribute of
#     the <extension>. Defaults to None. Usually the same as apiname.
#   addExtensions - regex matching names of additional extensions
#     to include. Defaults to None.
#   removeExtensions - regex matching names of extensions to
#     remove (after defaultExtensions and addExtensions). Defaults
#     to None.
#   emitExtensions - regex matching names of extensions to actually emit
#     interfaces for (though all requested versions are considered when
#     deciding which interfaces to generate).
#   sortProcedure - takes a list of FeatureInfo objects and sorts
#     them in place to a preferred order in the generated output.
#     Default is core API versions, ARB/KHR/OES extensions, all
#     other extensions, alphabetically within each group.
# The regex patterns can be None or empty, in which case they match
#   nothing.
class GeneratorOptions:
    """Represents options during header production from an API registry"""
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
                 sortProcedure = regSortFeatures):
        self.filename          = filename
        self.directory         = directory
        self.apiname           = apiname
        self.profile           = profile
        self.versions          = self.emptyRegex(versions)
        self.emitversions      = self.emptyRegex(emitversions)
        self.defaultExtensions = defaultExtensions
        self.addExtensions     = self.emptyRegex(addExtensions)
        self.removeExtensions  = self.emptyRegex(removeExtensions)
        self.emitExtensions    = self.emptyRegex(emitExtensions)
        self.sortProcedure     = sortProcedure
    #
    # Substitute a regular expression which matches no version
    # or extension names for None or the empty string.
    def emptyRegex(self,pat):
        if (pat == None or pat == ''):
            return '_nomatch_^'
        else:
            return pat

# OutputGenerator - base class for generating API interfaces.
# Manages basic logic, logging, and output file control
# Derived classes actually generate formatted output.
#
# ---- methods ----
# OutputGenerator(errFile, warnFile, diagFile)
#   errFile, warnFile, diagFile - file handles to write errors,
#     warnings, diagnostics to. May be None to not write.
# logMsg(level, *args) - log messages of different categories
#   level - 'error', 'warn', or 'diag'. 'error' will also
#     raise a UserWarning exception
#   *args - print()-style arguments
# setExtMap(map) - specify a dictionary map from extension names to
#   numbers, used in creating values for extension enumerants.
# makeDir(directory) - create a directory, if not already done.
#   Generally called from derived generators creating hierarchies.
# beginFile(genOpts) - start a new interface file
#   genOpts - GeneratorOptions controlling what's generated and how
# endFile() - finish an interface file, closing it when done
# beginFeature(interface, emit) - write interface for a feature
# and tag generated features as having been done.
#   interface - element for the <version> / <extension> to generate
#   emit - actually write to the header only when True
# endFeature() - finish an interface.
# genType(typeinfo,name,alias) - generate interface for a type
#   typeinfo - TypeInfo for a type
# genStruct(typeinfo,name,alias) - generate interface for a C "struct" type.
#   typeinfo - TypeInfo for a type interpreted as a struct
# genGroup(groupinfo,name,alias) - generate interface for a group of enums (C "enum")
#   groupinfo - GroupInfo for a group
# genEnum(enuminfo,name,alias) - generate interface for an enum (constant)
#   enuminfo - EnumInfo for an enum
#   name - enum name
# genCmd(cmdinfo,name,alias) - generate interface for a command
#   cmdinfo - CmdInfo for a command
# isEnumRequired(enumElem) - return True if this <enum> element is required
#   elem - <enum> element to test
# makeCDecls(cmd) - return C prototype and function pointer typedef for a
#     <command> Element, as a list of two strings
#   cmd - Element for the <command>
# newline() - print a newline to the output file (utility function)
#
class OutputGenerator:
    """Generate specified API interfaces in a specific style, such as a C header"""
    #
    # categoryToPath - map XML 'category' to include file directory name
    categoryToPath = {
        'bitmask'      : 'flags',
        'enum'         : 'enums',
        'funcpointer'  : 'funcpointers',
        'handle'       : 'handles',
        'define'       : 'defines',
        'basetype'     : 'basetypes',
    }
    #
    # Constructor
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        self.outFile = None
        self.errFile = errFile
        self.warnFile = warnFile
        self.diagFile = diagFile
        # Internal state
        self.featureName = None
        self.genOpts = None
        self.registry = None
        # Used for extension enum value generation
        self.extBase      = 1000000000
        self.extBlockSize = 1000
        self.madeDirs = {}
    #
    # logMsg - write a message of different categories to different
    #   destinations.
    # level -
    #   'diag' (diagnostic, voluminous)
    #   'warn' (warning)
    #   'error' (fatal error - raises exception after logging)
    # *args - print()-style arguments to direct to corresponding log
    def logMsg(self, level, *args):
        """Log a message at the given level. Can be ignored or log to a file"""
        if (level == 'error'):
            strfile = io.StringIO()
            write('ERROR:', *args, file=strfile)
            if (self.errFile != None):
                write(strfile.getvalue(), file=self.errFile)
            raise UserWarning(strfile.getvalue())
        elif (level == 'warn'):
            if (self.warnFile != None):
                write('WARNING:', *args, file=self.warnFile)
        elif (level == 'diag'):
            if (self.diagFile != None):
                write('DIAG:', *args, file=self.diagFile)
        else:
            raise UserWarning(
                '*** FATAL ERROR in Generator.logMsg: unknown level:' + level)
    #
    # enumToValue - parses and converts an <enum> tag into a value.
    # Returns a list
    #   first element - integer representation of the value, or None
    #       if needsNum is False. The value must be a legal number
    #       if needsNum is True.
    #   second element - string representation of the value
    # There are several possible representations of values.
    #   A 'value' attribute simply contains the value.
    #   A 'bitpos' attribute defines a value by specifying the bit
    #       position which is set in that value.
    #   A 'offset','extbase','extends' triplet specifies a value
    #       as an offset to a base value defined by the specified
    #       'extbase' extension name, which is then cast to the
    #       typename specified by 'extends'. This requires probing
    #       the registry database, and imbeds knowledge of the
    #       Vulkan extension enum scheme in this function.
    #   A 'alias' attribute contains the name of another enum
    #       which this is an alias of. The other enum must be
    #       declared first when emitting this enum.
    def enumToValue(self, elem, needsNum):
        name = elem.get('name')
        numVal = None
        if ('value' in elem.keys()):
            value = elem.get('value')
            # print('About to translate value =', value, 'type =', type(value))
            if (needsNum):
                numVal = int(value, 0)
            # If there's a non-integer, numeric 'type' attribute (e.g. 'u' or
            # 'ull'), append it to the string value.
            # t = enuminfo.elem.get('type')
            # if (t != None and t != '' and t != 'i' and t != 's'):
            #     value += enuminfo.type
            self.logMsg('diag', 'Enum', name, '-> value [', numVal, ',', value, ']')
            return [numVal, value]
        if ('bitpos' in elem.keys()):
            value = elem.get('bitpos')
            numVal = int(value, 0)
            numVal = 1 << numVal
            value = '0x%08x' % numVal
            self.logMsg('diag', 'Enum', name, '-> bitpos [', numVal, ',', value, ']')
            return [numVal, value]
        if ('offset' in elem.keys()):
            # Obtain values in the mapping from the attributes
            enumNegative = False
            offset = int(elem.get('offset'),0)
            extnumber = int(elem.get('extnumber'),0)
            extends = elem.get('extends')
            if ('dir' in elem.keys()):
                enumNegative = True
            self.logMsg('diag', 'Enum', name, 'offset =', offset,
                'extnumber =', extnumber, 'extends =', extends,
                'enumNegative =', enumNegative)
            # Now determine the actual enumerant value, as defined
            # in the "Layers and Extensions" appendix of the spec.
            numVal = self.extBase + (extnumber - 1) * self.extBlockSize + offset
            if (enumNegative):
                numVal = -numVal
            value = '%d' % numVal
            # More logic needed!
            self.logMsg('diag', 'Enum', name, '-> offset [', numVal, ',', value, ']')
            return [numVal, value]
        if 'alias' in elem.keys():
            return [None, elem.get('alias')]
        return [None, None]
    #
    # checkDuplicateEnums - sanity check for enumerated values
    #   enums - list of <enum> Elements
    #   returns the list with duplicates stripped
    def checkDuplicateEnums(self, enums):
        # Dictionaries indexed by name and numeric value.
        # Entries are [ Element, numVal, strVal ] matching name or value

        nameMap = {}
        valueMap = {}

        stripped = []
        for elem in enums:
            name = elem.get('name')
            (numVal, strVal) = self.enumToValue(elem, True)

            if name in nameMap:
                # Duplicate name found; check values
                (name2, numVal2, strVal2) = nameMap[name]

                # Duplicate enum values for the same name are benign. This
                # happens when defining the same enum conditionally in
                # several extension blocks.
                if (strVal2 == strVal or (numVal != None and
                    numVal == numVal2)):
                    True
                    # self.logMsg('info', 'checkDuplicateEnums: Duplicate enum (' + name +
                    #             ') found with the same value:' + strVal)
                else:
                    self.logMsg('warn', 'checkDuplicateEnums: Duplicate enum (' + name +
                                ') found with different values:' + strVal +
                                ' and ' + strVal2)

                # Don't add the duplicate to the returned list
                continue
            elif numVal in valueMap:
                # Duplicate value found (such as an alias); report it, but
                # still add this enum to the list.
                (name2, numVal2, strVal2) = valueMap[numVal]

                try:
                    self.logMsg('warn', 'Two enums found with the same value: '
                             + name + ' = ' + name2.get('name') + ' = ' + strVal)
                except:
                    pdb.set_trace()

            # Track this enum to detect followon duplicates
            nameMap[name] = [ elem, numVal, strVal ]
            if numVal != None:
                valueMap[numVal] = [ elem, numVal, strVal ]

            # Add this enum to the list
            stripped.append(elem)

        # Return the list
        return stripped
    #
    def makeDir(self, path):
        self.logMsg('diag', 'OutputGenerator::makeDir(' + path + ')')
        if not (path in self.madeDirs.keys()):
            # This can get race conditions with multiple writers, see
            # https://stackoverflow.com/questions/273192/
            if not os.path.exists(path):
                os.makedirs(path)
            self.madeDirs[path] = None
    #
    def beginFile(self, genOpts):
        self.genOpts = genOpts
        #
        # Open specified output file. Not done in constructor since a
        # Generator can be used without writing to a file.
        if (self.genOpts.filename != None):
            filename = self.genOpts.directory + '/' + self.genOpts.filename
            self.outFile = io.open(filename, 'w', encoding='utf-8')
        else:
            self.outFile = sys.stdout
    def endFile(self):
        self.errFile and self.errFile.flush()
        self.warnFile and self.warnFile.flush()
        self.diagFile and self.diagFile.flush()
        self.outFile.flush()
        if (self.outFile != sys.stdout and self.outFile != sys.stderr):
            self.outFile.close()
        self.genOpts = None
    #
    def beginFeature(self, interface, emit):
        self.emit = emit
        self.featureName = interface.get('name')
        # If there's an additional 'protect' attribute in the feature, save it
        self.featureExtraProtect = interface.get('protect')
    def endFeature(self):
        # Derived classes responsible for emitting feature
        self.featureName = None
        self.featureExtraProtect = None
    # Utility method to validate we're generating something only inside a
    # <feature> tag
    def validateFeature(self, featureType, featureName):
        if (self.featureName == None):
            raise UserWarning('Attempt to generate', featureType,
                    featureName, 'when not in feature')
    #
    # Type generation
    def genType(self, typeinfo, name, alias):
        self.validateFeature('type', name)
    #
    # Struct (e.g. C "struct" type) generation
    def genStruct(self, typeinfo, name, alias):
        self.validateFeature('struct', name)

        # The mixed-mode <member> tags may contain no-op <comment> tags.
        # It is convenient to remove them here where all output generators
        # will benefit.
        for member in typeinfo.elem.findall('.//member'):
            for comment in member.findall('comment'):
                member.remove(comment)
    #
    # Group (e.g. C "enum" type) generation
    def genGroup(self, groupinfo, name, alias):
        self.validateFeature('group', name)
    #
    # Enumerant (really, constant) generation
    def genEnum(self, enuminfo, name, alias):
        self.validateFeature('enum', name)
    #
    # Command generation
    def genCmd(self, cmd, name, alias):
        self.validateFeature('command', name)
    #
    # Utility functions - turn a <proto> <name> into C-language prototype
    # and typedef declarations for that name.
    # name - contents of <name> tag
    # tail - whatever text follows that tag in the Element
    def makeProtoName(self, name, tail):
        return self.genOpts.apientry + name + tail
    def makeTypedefName(self, name, tail):
       return '(' + self.genOpts.apientryp + 'PFN_' + name + tail + ')'
    #
    # makeCParamDecl - return a string which is an indented, formatted
    # declaration for a <param> or <member> block (e.g. function parameter
    # or structure/union member).
    # param - Element (<param> or <member>) to format
    # aligncol - if non-zero, attempt to align the nested <name> element
    #   at this column
    def makeCParamDecl(self, param, aligncol):
        paramdecl = '    ' + noneStr(param.text)
        for elem in param:
            text = noneStr(elem.text)
            tail = noneStr(elem.tail)
            if (elem.tag == 'name' and aligncol > 0):
                self.logMsg('diag', 'Aligning parameter', elem.text, 'to column', self.genOpts.alignFuncParam)
                # Align at specified column, if possible
                paramdecl = paramdecl.rstrip()
                oldLen = len(paramdecl)
                # This works around a problem where very long type names -
                # longer than the alignment column - would run into the tail
                # text.
                paramdecl = paramdecl.ljust(aligncol-1) + ' '
                newLen = len(paramdecl)
                self.logMsg('diag', 'Adjust length of parameter decl from', oldLen, 'to', newLen, ':', paramdecl)
            paramdecl += text + tail
        return paramdecl
    #
    # getCParamTypeLength - return the length of the type field is an indented, formatted
    # declaration for a <param> or <member> block (e.g. function parameter
    # or structure/union member).
    # param - Element (<param> or <member>) to identify
    def getCParamTypeLength(self, param):
        paramdecl = '    ' + noneStr(param.text)
        for elem in param:
            text = noneStr(elem.text)
            tail = noneStr(elem.tail)
            if (elem.tag == 'name'):
                # Align at specified column, if possible
                newLen = len(paramdecl.rstrip())
                self.logMsg('diag', 'Identifying length of', elem.text, 'as', newLen)
            paramdecl += text + tail
        return newLen
    #
    # isEnumRequired(elem) - return True if this <enum> element is
    # required, False otherwise
    # elem - <enum> element to test
    def isEnumRequired(self, elem):
        required = elem.get('required') != None
        self.logMsg('diag', 'isEnumRequired:', elem.get('name'),
            '->', required)
        return required

        #@@@ This code is overridden by equivalent code now run in
        #@@@ Registry.generateFeature

        required = False

        extname = elem.get('extname')
        if extname is not None:
            # 'supported' attribute was injected when the <enum> element was
            # moved into the <enums> group in Registry.parseTree()
            if self.genOpts.defaultExtensions == elem.get('supported'):
                required = True
            elif re.match(self.genOpts.addExtensions, extname) is not None:
                required = True
        elif elem.get('version') is not None:
            required = re.match(self.genOpts.emitversions, elem.get('version')) is not None
        else:
            required = True

        return required

    #
    # makeCDecls - return C prototype and function pointer typedef for a
    #   command, as a two-element list of strings.
    # cmd - Element containing a <command> tag
    def makeCDecls(self, cmd):
        """Generate C function pointer typedef for <command> Element"""
        proto = cmd.find('proto')
        params = cmd.findall('param')
        # Begin accumulating prototype and typedef strings
        pdecl = self.genOpts.apicall
        tdecl = 'typedef '
        #
        # Insert the function return type/name.
        # For prototypes, add APIENTRY macro before the name
        # For typedefs, add (APIENTRY *<name>) around the name and
        #   use the PFN_cmdnameproc naming convention.
        # Done by walking the tree for <proto> element by element.
        # etree has elem.text followed by (elem[i], elem[i].tail)
        #   for each child element and any following text
        # Leading text
        pdecl += noneStr(proto.text)
        tdecl += noneStr(proto.text)
        # For each child element, if it's a <name> wrap in appropriate
        # declaration. Otherwise append its contents and tail contents.
        for elem in proto:
            text = noneStr(elem.text)
            tail = noneStr(elem.tail)
            if (elem.tag == 'name'):
                pdecl += self.makeProtoName(text, tail)
                tdecl += self.makeTypedefName(text, tail)
            else:
                pdecl += text + tail
                tdecl += text + tail
        # Now add the parameter declaration list, which is identical
        # for prototypes and typedefs. Concatenate all the text from
        # a <param> node without the tags. No tree walking required
        # since all tags are ignored.
        # Uses: self.indentFuncProto
        # self.indentFuncPointer
        # self.alignFuncParam
        # Might be able to doubly-nest the joins, e.g.
        #   ','.join(('_'.join([l[i] for i in range(0,len(l))])
        n = len(params)
        # Indented parameters
        if n > 0:
            indentdecl = '(\n'
            for i in range(0,n):
                paramdecl = self.makeCParamDecl(params[i], self.genOpts.alignFuncParam)
                if (i < n - 1):
                    paramdecl += ',\n'
                else:
                    paramdecl += ');'
                indentdecl += paramdecl
        else:
            indentdecl = '(void);'
        # Non-indented parameters
        paramdecl = '('
        if n > 0:
            for i in range(0,n):
                paramdecl += ''.join([t for t in params[i].itertext()])
                if (i < n - 1):
                    paramdecl += ', '
        else:
            paramdecl += 'void'
        paramdecl += ");";
        return [ pdecl + indentdecl, tdecl + paramdecl ]
    #
    def newline(self):
        write('', file=self.outFile)

    def setRegistry(self, registry):
        self.registry = registry
        #
