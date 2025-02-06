# Copyright 2023 The BoringSSL Authors
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# binary_dir_relative_path sets outvar to
# ${CMAKE_CURRENT_BINARY_DIR}/${cur_bin_dir_relative}, but expressed relative to
# ${CMAKE_BINARY_DIR}.
#
# TODO(davidben): When we require CMake 3.20 or later, this can be replaced with
# the built-in cmake_path(RELATIVE_PATH) function.
function(binary_dir_relative_path cur_bin_dir_relative outvar)
  string(LENGTH "${CMAKE_BINARY_DIR}/" root_dir_length)
  string(SUBSTRING "${CMAKE_CURRENT_BINARY_DIR}/${cur_bin_dir_relative}" ${root_dir_length} -1 result)
  set(${outvar} ${result} PARENT_SCOPE)
endfunction()

# copy_post_build causes targets in ${ARGN} to be copied to
# ${CMAKE_CURRENT_BINARY_DIR}/${dir} after being built.
function(copy_post_build dir)
  foreach(target ${ARGN})
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${dir}"
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> "${CMAKE_CURRENT_BINARY_DIR}/${dir}")
  endforeach()
endfunction()
