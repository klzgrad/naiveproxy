#!/usr/bin/python2
#
# Copyright 2017 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# run_code_generation.py:
#   Runs ANGLE format table and other script code generation scripts.

import hashlib
import json
import os
import subprocess
import sys

script_dir = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))
root_dir = os.path.abspath(os.path.join(script_dir, '..'))

# auto_script is a standard way for scripts to return their inputs and outputs.

def get_child_script_dirname(script):
    # All script names are relative to ANGLE's root
    return os.path.dirname(os.path.abspath(os.path.join(root_dir, script)))

# Replace all backslashes with forward slashes to be platform independent
def clean_path_slashes(path):
    return path.replace("\\", "/")

# Takes a script input file name which is relative to the code generation script's directory and
# changes it to be relative to the angle root directory
def rebase_script_input_path(script_path, input_file_path):
    return os.path.relpath(os.path.join(os.path.dirname(script_path), input_file_path), root_dir);

def grab_from_script(script, param):
    res = subprocess.check_output(['python', script, param]).strip()
    return [clean_path_slashes(rebase_script_input_path(script, name)) for name in res.split(',')]

def auto_script(script):
    # Set the CWD to the script directory.
    os.chdir(get_child_script_dirname(script))
    base_script = os.path.basename(script)
    return {
        'script': script,
        'inputs': grab_from_script(base_script, 'inputs'),
    }

hash_fname = "run_code_generation_hashes.json"

# TODO(jmadill): Convert everyting to auto-script.
generators = {
    'ANGLE format': {
        'inputs': [
            'src/libANGLE/renderer/angle_format.py',
            'src/libANGLE/renderer/angle_format_data.json',
            'src/libANGLE/renderer/angle_format_map.json',
        ],
        'script': 'src/libANGLE/renderer/gen_angle_format_table.py',
    },
    'ANGLE load functions table': {
        'inputs': [
            'src/libANGLE/renderer/load_functions_data.json',
        ],
        'script': 'src/libANGLE/renderer/gen_load_functions_table.py',
    },
    'D3D11 blit shader selection': {
        'inputs': [],
        'script': 'src/libANGLE/renderer/d3d/d3d11/gen_blit11helper.py',
    },
    'D3D11 format': {
        'inputs': [
            'src/libANGLE/renderer/angle_format.py',
            'src/libANGLE/renderer/d3d/d3d11/texture_format_data.json',
            'src/libANGLE/renderer/d3d/d3d11/texture_format_map.json',
        ],
        'script': 'src/libANGLE/renderer/d3d/d3d11/gen_texture_format_table.py',
    },
    'DXGI format': {
        'inputs': [
            'src/libANGLE/renderer/angle_format.py',
            'src/libANGLE/renderer/angle_format_map.json',
            'src/libANGLE/renderer/d3d/d3d11/dxgi_format_data.json',
            'src/libANGLE/renderer/gen_angle_format_table.py',
        ],
        'script': 'src/libANGLE/renderer/d3d/d3d11/gen_dxgi_format_table.py',
    },
    'DXGI format support': {
        'inputs': [
            'src/libANGLE/renderer/d3d/d3d11/dxgi_support_data.json',
        ],
        'script': 'src/libANGLE/renderer/d3d/d3d11/gen_dxgi_support_tables.py',
    },
    'GL/EGL/WGL loader':
        auto_script('scripts/generate_loader.py'),
    'GL/EGL entry points':
        auto_script('scripts/generate_entry_points.py'),
    'GL copy conversion table': {
        'inputs': [
            'src/libANGLE/es3_copy_conversion_formats.json',
        ],
        'script': 'src/libANGLE/gen_copy_conversion_table.py',
    },
    'GL format map': {
        'inputs': [
            'src/libANGLE/es3_format_type_combinations.json',
            'src/libANGLE/format_map_data.json',
        ],
        'script': 'src/libANGLE/gen_format_map.py',
    },
    'uniform type': {
        'inputs': [],
        'script': 'src/common/gen_uniform_type_table.py',
    },
    'OpenGL dispatch table': {
        'inputs': [
            'scripts/gl.xml',
        ],
        'script': 'src/libANGLE/renderer/gl/generate_gl_dispatch_table.py',
    },
    'packed enum': {
        'inputs': [
            'src/common/packed_gl_enums.json',
            'src/common/packed_egl_enums.json',
        ],
        'script': 'src/common/gen_packed_gl_enums.py',
    },
    'proc table': {
        'inputs': [
            'src/libGLESv2/proc_table_data.json',
        ],
        'script': 'src/libGLESv2/gen_proc_table.py',
    },
    'Vulkan format': {
        'inputs': [
            'src/libANGLE/renderer/angle_format.py',
            'src/libANGLE/renderer/angle_format_map.json',
            'src/libANGLE/renderer/vulkan/vk_format_map.json',
        ],
        'script': 'src/libANGLE/renderer/vulkan/gen_vk_format_table.py',
    },
    'Vulkan mandatory format support table': {
        'inputs': [
            'src/libANGLE/renderer/angle_format.py',
            'third_party/vulkan-headers/src/registry/vk.xml',
            'src/libANGLE/renderer/vulkan/vk_mandatory_format_support_data.json',
        ],
        'script': 'src/libANGLE/renderer/vulkan/gen_vk_mandatory_format_support_table.py',
    },
    'Vulkan internal shader programs':
        auto_script('src/libANGLE/renderer/vulkan/gen_vk_internal_shaders.py'),
    'Emulated HLSL functions': {
        'inputs': [
            'src/compiler/translator/emulated_builtin_function_data_hlsl.json'
        ],
        'script': 'src/compiler/translator/gen_emulated_builtin_function_tables.py'
    },
    'ESSL static builtins': {
        'inputs': [
            'src/compiler/translator/builtin_function_declarations.txt',
            'src/compiler/translator/builtin_variables.json',
        ],
        'script': 'src/compiler/translator/gen_builtin_symbols.py',
    },
}


def md5(fname):
    hash_md5 = hashlib.md5()
    with open(fname, "r") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()


def any_input_dirty(name, inputs, new_hashes, old_hashes):
    found_dirty_input = False
    for finput in inputs:
        key = name + ":" + finput
        new_hashes[key] = md5(finput)
        if (not key in old_hashes) or (old_hashes[key] != new_hashes[key]):
            found_dirty_input = True
    return found_dirty_input


def any_old_hash_missing(new_hashes, old_hashes):
    for name, _ in old_hashes.iteritems():
        if name not in new_hashes:
            return True
    return False


def main():
    os.chdir(script_dir)
    old_hashes = json.load(open(hash_fname))
    new_hashes = {}
    any_dirty = False

    verify_only = False
    if len(sys.argv) > 1 and sys.argv[1] == '--verify-no-dirty':
        verify_only = True

    for name, info in sorted(generators.iteritems()):

        # Reset the CWD to the root ANGLE directory.
        os.chdir(root_dir)
        script = info['script']

        if any_input_dirty(name, info['inputs'] + [script], new_hashes, old_hashes):
            any_dirty = True

            if not verify_only:
                # Set the CWD to the script directory.
                os.chdir(get_child_script_dirname(script))

                print('Running ' + name + ' code generator')
                if subprocess.call(['python', os.path.basename(script)]) != 0:
                    sys.exit(1)

    if any_old_hash_missing(new_hashes, old_hashes):
        any_dirty = True

    if verify_only:
        sys.exit(any_dirty)

    if any_dirty:
        args = []
        if os.name == 'nt':
            args += ['git.bat']
        else:
            args += ['git']
        # The diff can be so large the arguments to clang-format can break the Windows command
        # line length limits. Work around this by calling git cl format with --full.
        args += ['cl', 'format', '--full']
        print('Calling git cl format')
        subprocess.call(args)

        os.chdir(script_dir)
        json.dump(new_hashes, open(hash_fname, "w"), indent=2, sort_keys=True,
                  separators=(',', ':\n    '))


if __name__ == '__main__':
    sys.exit(main())
