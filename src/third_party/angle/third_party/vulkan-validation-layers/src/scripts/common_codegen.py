#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2017, 2019 The Khronos Group Inc.
# Copyright (c) 2015-2017, 2019 Valve Corporation
# Copyright (c) 2015-2017, 2019 LunarG, Inc.
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
# Author: Mark Lobodzinski <mark@lunarg.com>

import os,re,sys,string
import xml.etree.ElementTree as etree
from collections import namedtuple, OrderedDict

# Copyright text prefixing all headers (list of strings).
prefixStrings = [
    '/*',
    '** Copyright (c) 2015-2017, 2019 The Khronos Group Inc.',
    '** Copyright (c) 2015-2017, 2019 Valve Corporation',
    '** Copyright (c) 2015-2017, 2019 LunarG, Inc.',
    '** Copyright (c) 2015-2017, 2019 Google Inc.',
    '**',
    '** Licensed under the Apache License, Version 2.0 (the "License");',
    '** you may not use this file except in compliance with the License.',
    '** You may obtain a copy of the License at',
    '**',
    '**     http://www.apache.org/licenses/LICENSE-2.0',
    '**',
    '** Unless required by applicable law or agreed to in writing, software',
    '** distributed under the License is distributed on an "AS IS" BASIS,',
    '** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.',
    '** See the License for the specific language governing permissions and',
    '** limitations under the License.',
    '*/',
    ''
]


platform_dict = {
    'android' : 'VK_USE_PLATFORM_ANDROID_KHR',
    'fuchsia' : 'VK_USE_PLATFORM_FUCHSIA',
    'ggp': 'VK_USE_PLATFORM_GGP',
    'ios' : 'VK_USE_PLATFORM_IOS_MVK',
    'macos' : 'VK_USE_PLATFORM_MACOS_MVK',
    'metal' : 'VK_USE_PLATFORM_METAL_EXT',
    'vi' : 'VK_USE_PLATFORM_VI_NN',
    'wayland' : 'VK_USE_PLATFORM_WAYLAND_KHR',
    'win32' : 'VK_USE_PLATFORM_WIN32_KHR',
    'xcb' : 'VK_USE_PLATFORM_XCB_KHR',
    'xlib' : 'VK_USE_PLATFORM_XLIB_KHR',
    'xlib_xrandr' : 'VK_USE_PLATFORM_XLIB_XRANDR_EXT',
}

#
# Return appropriate feature protect string from 'platform' tag on feature
def GetFeatureProtect(interface):
    """Get platform protection string"""
    platform = interface.get('platform')
    protect = None
    if platform is not None:
        protect = platform_dict[platform]
    return protect

# Return a dict containing the dispatchable/non-dispatchable type of every handle
def GetHandleTypes(tree):
    # Extend OrderedDict with common handle operations
    class HandleDict(OrderedDict):
        def IsDispatchable(self, handle_type):
            return self.get(handle_type) == 'VK_DEFINE_HANDLE'
        def IsNonDispatchable(self, handle_type):
            return self.get(handle_type) == 'VK_DEFINE_NON_DISPATCHABLE_HANDLE'

    handles = HandleDict()
    for elem in tree.findall("types/type/[@category='handle']"):
        if not elem.get('alias'):
            name = elem.get('name')
            handles[name] = elem.find('type').text
    return handles

# Return a dict containing the parent of every handle
def GetHandleParents(tree):
    # Extend OrderedDict with common handle operations
    class HandleParentDict(OrderedDict):
        def IsParentDevice(self, handle_type):
            next_object = self.get(handle_type)
            while next_object != 'VkDevice' and next_object != 'VkInstance' and next_object != 'VkPhysicalDevice' and next_object is not None:
                next_object = self.get(next_object)
            return next_object == 'VkDevice'
        def GetHandleParent(self, handle_type):
            return self.get(handle_type)

    handle_parents = HandleParentDict()
    for elem in tree.findall("types/type/[@category='handle']"):
        if not elem.get('alias') or not elem.get('parent'):
            name = elem.get('name')
            handle_parents[name] = elem.get('parent')
    return handle_parents

# Return a dict containing the category attribute of every type
def GetTypeCategories(tree):
    type_categories = OrderedDict()
    for elem in tree.findall("types/type"):
        if not elem.get('alias'):
            # name is either an attribute or the text of a child <name> tag
            name = elem.get('name') or (elem.find("name") and elem.find('name').text)
            type_categories[name] = elem.get('category')
    return type_categories

# Treats outdents a multiline string by the leading whitespace on the first line
# Optionally indenting by the given prefix
def Outdent(string_in, indent=''):
    string_out = re.sub('^ *', '', string_in) # kill stray  leading spaces
    if string_out[0] != '\n':
        return string_in # needs new line to find the first line's indent level

    first_indent = string_out[1:]
    fake_indent = '\n' + ' ' * (len(first_indent) - len(first_indent.lstrip()))
    indent = '\n' + indent

    string_out = string_out.rstrip() + '\n' # remove trailing whitespace except for a newline
    outdent = re.sub(fake_indent, indent, string_out)
    return outdent[1:]


# helper to define paths relative to the repo root
def repo_relative(path):
    return os.path.abspath(os.path.join(os.path.dirname(__file__), '..', path))

