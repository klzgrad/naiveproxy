# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# GN version: third_party/closure_compiler/closure_args.gni
{
  'default_closure_args': [
    'compilation_level=SIMPLE_OPTIMIZATIONS',

    # Keep this in sync with chrome/browser/web_dev_style/js_checker.py.
    'extra_annotation_name=attribute',
    'extra_annotation_name=demo',
    'extra_annotation_name=element',
    'extra_annotation_name=group',
    'extra_annotation_name=hero',
    'extra_annotation_name=homepage',
    'extra_annotation_name=status',
    'extra_annotation_name=submodule',

    'jscomp_error=accessControls',
    'jscomp_error=ambiguousFunctionDecl',
    'jscomp_error=checkTypes',
    'jscomp_error=checkVars',
    'jscomp_error=constantProperty',
    'jscomp_error=deprecated',
    'jscomp_error=externsValidation',
    'jscomp_error=globalThis',
    'jscomp_error=invalidCasts',
    'jscomp_error=missingProperties',
    'jscomp_error=missingReturn',
    'jscomp_error=nonStandardJsDocs',
    'jscomp_error=suspiciousCode',
    'jscomp_error=undefinedNames',
    'jscomp_error=undefinedVars',
    'jscomp_error=unknownDefines',
    'jscomp_error=uselessCode',
    'jscomp_error=visibility',

    'language_in=ECMASCRIPT_NEXT',
    'language_out=ECMASCRIPT5_STRICT',

    'checks_only',
    'chrome_pass',
    'polymer_pass',

    'source_map_format=V3',
  ],

  'default_disabled_closure_args': [
    # TODO(dbeam): happens when the same file is <include>d multiple times.
    'jscomp_off=duplicate',
  ],
}
