" Copyright 2014 The Chromium Authors. All rights reserved.
" Use of this source code is governed by a BSD-style license that can be
" found in the LICENSE file.
"
" gn.vim: Vim syntax file for GN.
"
" Quit when a (custom) syntax file was already loaded
"if exists("b:current_syntax")
  "finish
"endif

syn case match

" Keywords within functions
syn keyword     gnConditional       if else
hi def link     gnConditional       Conditional

" Predefined variables
syn keyword     gnPredefVar current_cpu current_os current_toolchain
syn keyword     gnPredefVar default_toolchain host_cpu host_os
syn keyword     gnPredefVar root_build_dir root_gen_dir root_out_dir
syn keyword     gnPredefVar target_cpu target_gen_dir target_out_dir
syn keyword     gnPredefVar target_os
syn keyword     gnPredefVar true false
hi def link     gnPredefVar         Constant

" Target declarations
syn keyword     gnTarget action action_foreach copy executable group
syn keyword     gnTarget shared_library source_set static_library
syn keyword     gnTarget loadable_module
hi def link     gnTarget            Type

" Buildfile functions
syn keyword     gnFunctions assert config declare_args defined exec_script
syn keyword     gnFunctions foreach get_label_info get_path_info
syn keyword     gnFunctions get_target_outputs getenv import print
syn keyword     gnFunctions process_file_template read_file rebase_path
syn keyword     gnFunctions set_default_toolchain set_defaults
syn keyword     gnFunctions set_sources_assignment_filter template tool
syn keyword     gnFunctions toolchain toolchain_args write_file
hi def link     gnFunctions         Macro

" Variables
syn keyword     gnVariable all_dependent_configs allow_circular_includes_from
syn keyword     gnVariable args asmflags cflags cflags_c cflags_cc cflags_objc
syn keyword     gnVariable cflags_objcc check_includes complete_static_lib
syn keyword     gnVariable configs data data_deps defines depfile deps
syn keyword     gnVariable include_dirs inputs ldflags lib_dirs libs
syn keyword     gnVariable output_extension output_name outputs public
syn keyword     gnVariable public_configs public_deps scripte sources testonly
syn keyword     gnVariable visibility
hi def link     gnVariable          Keyword

" Strings
syn region      gnString start=+L\="+ skip=+\\\\\|\\"+ end=+"+ contains=@Spell,gnTargetName
syn match       gnTargetName '\v:[^"]+' contained
hi def link     gnString            String
hi def link     gnTargetName        Special

" Comments
syn keyword     gnTodo              contained TODO FIXME XXX BUG NOTE
syn cluster     gnCommentGroup      contains=gnTodo
syn region      gnComment           start="#" end="$" contains=@gnCommentGroup,@Spell

hi def link     gnComment           Comment
hi def link     gnTodo              Todo

" Operators; I think this is a bit too colourful.
"syn match gnOperator /=/
"syn match gnOperator /!=/
"syn match gnOperator />=/
"syn match gnOperator /<=/
"syn match gnOperator /==/
"syn match gnOperator /+=/
"syn match gnOperator /-=/
"syn match gnOperator /\s>\s/
"syn match gnOperator /\s<\s/
"syn match gnOperator /\s+\s/
"syn match gnOperator /\s-\s/
"hi def link     gnOperator          Operator

syn sync minlines=500

let b:current_syntax = "gn"
