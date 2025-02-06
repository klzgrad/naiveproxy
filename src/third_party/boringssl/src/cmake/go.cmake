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

# Go is an optional dependency. It's a necessary dependency if running tests or
# the FIPS build, which will check these.
find_program(GO_EXECUTABLE go)

function(require_go)
  if(NOT GO_EXECUTABLE)
    message(FATAL_ERROR "Could not find Go")
  endif()
endfunction()

function(go_executable dest package)
  require_go()
  set(godeps "${PROJECT_SOURCE_DIR}/util/godeps.go")
  if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    # The DEPFILE parameter to add_custom_command only works with Ninja. Query
    # the sources at configure time. Additionally, everything depends on go.mod.
    # That affects what external packages to use.
    #
    # TODO(davidben): Starting CMake 3.20, it also works with Make. Starting
    # 3.21, it works with Visual Studio and Xcode too.
    execute_process(COMMAND ${GO_EXECUTABLE} run ${godeps} -format cmake
                            -pkg ${package}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE sources
                    RESULT_VARIABLE godeps_result)
    add_custom_command(OUTPUT ${dest}
                       COMMAND ${GO_EXECUTABLE} build
                               -o ${CMAKE_CURRENT_BINARY_DIR}/${dest} ${package}
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                       DEPENDS ${sources} ${PROJECT_SOURCE_DIR}/go.mod)
  else()
    # Ninja expects the target in the depfile to match the output. This is a
    # relative path from the build directory.
    binary_dir_relative_path(${dest} target)

    set(depfile "${CMAKE_CURRENT_BINARY_DIR}/${dest}.d")
    add_custom_command(OUTPUT ${dest}
                       COMMAND ${GO_EXECUTABLE} build
                               -o ${CMAKE_CURRENT_BINARY_DIR}/${dest} ${package}
                       COMMAND ${GO_EXECUTABLE} run ${godeps} -format depfile
                               -target ${target} -pkg ${package} -out ${depfile}
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                       DEPENDS ${godeps} ${PROJECT_SOURCE_DIR}/go.mod
                       DEPFILE ${depfile})
  endif()
endfunction()

