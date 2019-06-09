# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This script is used to convert a list of references to corresponding ProGuard
keep rules, for the purposes of maintaining compatibility between async DFMs
and synchronously proguarded modules.
This script take an input file generated from
//build/android/bytecode/java/org/chromium/bytecode/ByteCodeProcessor.java
during the build phase of an async module.
"""

from collections import defaultdict
import argparse
import sys

# Classes in _IGNORED_PACKAGES do not need explicit keep rules because they are
# system APIs and are already included in ProGuard configs.
_IGNORED_PACKAGES = ['java', 'android', 'org.w3c', 'org.xml', 'dalvik']

# Mapping for translating Java bytecode type identifiers to source code type
# identifiers.
_TYPE_IDENTIFIER_MAP = {
    'V': 'void',
    'Z': 'boolean',
    'B': 'byte',
    'S': 'short',
    'C': 'char',
    'I': 'int',
    'J': 'long',
    'F': 'float',
    'D': 'double',
}


# Translates DEX TypeDescriptor of the first type found in a given string to
# its source code type identifier, as described in
# https://source.android.com/devices/tech/dalvik/dex-format#typedescriptor,
# and returns the translated type and the starting index of the next type
# (if present).
def translate_single_type(typedesc):
  array_count = 0
  translated = ''
  next_index = 0

  # In the constant pool, fully qualified names (prefixed by 'L') have a
  # trailing ';' if they are describing the type/return type of a symbol,
  # or the type of arguments passed to a symbol. TypeDescriptor representing
  # primitive types do not have trailing ';'s in any circumstances.
  for i, c in enumerate(typedesc):
    if c == '[':
      array_count += 1
      continue
    if c == 'L':
      # Fully qualified names have no trailing ';' if they are describing the
      # containing class of a reference.
      next_index = typedesc.find(';')
      if next_index == -1:
        next_index = len(typedesc)
      translated = typedesc[i + 1:next_index]
      break
    else:
      translated = _TYPE_IDENTIFIER_MAP[c]
      next_index = i
      break

  translated += '[]' * array_count
  return translated, next_index + 1


# Convert string of method argument types read from constant pool to
# corresponding list of srouce code type identifiers.
def parse_args_list(args_list):
  parsed_args = []
  start_index = 0

  while start_index < len(args_list):
    args_list = args_list[start_index:]
    translated_arg, start_index = translate_single_type(args_list)
    parsed_args.append(translated_arg)

  return parsed_args


def add_to_refs(class_name, keep_entry, dep_refs):
  # Add entry to class's keep rule if entry is not the empty string
  if class_name in dep_refs and keep_entry:
    dep_refs[class_name].append(keep_entry)
  else:
    dep_refs[class_name] = [keep_entry]


def main(argv):
  dep_refs = defaultdict(list)
  extended_and_implemented_classes = set()

  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--input-file',
      required=True,
      help='Path to constant pool reference output.')
  parser.add_argument(
      '--output-file',
      required=True,
      help='Path to write corresponding keep rules to')
  args = parser.parse_args(argv[1:])

  with open(args.input_file, 'r') as constant_pool_refs:
    for line in constant_pool_refs:
      line = line.rstrip().replace('/', '.')
      # Ignore any references specified by the list of _IGNORED_PACKAGES.
      if any(line.startswith(package) for package in _IGNORED_PACKAGES):
        continue

      reflist = line.split(',')

      # Lines denoting super classes and implemented interface references do
      # not contain additional information and thus have reflist size 1.
      # Store these as a separate set as they require full keep rules.
      if len(reflist) == 1:
        extended_and_implemented_classes.add(reflist[0])
        continue

      class_name = reflist[0]
      member_name = reflist[1]
      member_info = reflist[2]
      keep_entry = ''

      # When testing with the VR module, all class names read from constant
      # pool output that were prefixed with '[' matched references to the
      # overridden clone() method of the Object class. These seem to correspond
      # to Java enum types defined within classes.
      # It is not entirely clear whether or not this always represents
      # an enum, why enums would be represented as such in the constant pool,
      # or how we should go about keeping these references. For the moment,
      # ignoring these references does not impact compatibility between
      # modules.
      if class_name.startswith('['):
        continue

      # If member_info starts with '(', member is a method, otherwise member
      # is a field.
      # Format keep entries as per ProGuard documentation
      # guardsquare.com/en/products/proguard/manual/usage#classspecification.
      if member_info.startswith('('):
        args_list, return_type = member_info.split(')')
        args_list = parse_args_list(args_list[1:])
        if member_name == '<init>':
          # No return type specified for constructors.
          return_type = ''
        else:
          return_type = translate_single_type(return_type)[0]
        keep_entry = '%s %s(%s);' % (return_type, member_name,
                                     ', '.join(args_list))
      else:
        keep_entry = '%s %s;' % (translate_single_type(member_info)[0],
                                 member_name)

      dep_refs[class_name].append(keep_entry)

  with open(args.output_file, 'w') as keep_rules:
    # Write super classes and implemented interfaces to keep rules.
    for super_class in sorted(extended_and_implemented_classes):
      keep_rules.write(
          '-keep,allowobfuscation class %s { *; }\n' % (super_class.rstrip()))
      keep_rules.write('\n')
    # Write all other class references to keep rules.
    for c in sorted(dep_refs.iterkeys()):
      class_keeps = '\n  '.join(dep_refs[c])
      keep_rules.write(
          '-keep,allowobfuscation class %s {\n  %s\n}\n' % (c, class_keeps))
      keep_rules.write('\n')


if __name__ == '__main__':
  main(sys.argv)
