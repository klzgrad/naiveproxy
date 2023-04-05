# Additional interop for things like macros and inlined functions.
add_library(rust_wrapper STATIC rust_wrapper.c)
target_link_libraries(rust_wrapper crypto)

# Generate architecture-specific wrappers.
set(WRAPPER_TARGET ${CMAKE_BINARY_DIR}/rust/src/wrapper_${RUST_BINDINGS}.rs)
set(COMMAND ${BINDGEN_EXECUTABLE} "wrapper.h"
            -o ${WRAPPER_TARGET}
            --no-derive-default
            --enable-function-attribute-detection
            --use-core
            --size_t-is-usize
            --default-macro-constant-type="signed"
            --rustified-enum="point_conversion_form_t"
            # These are not BoringSSL symbols, they are from glibc
            # and are not relevant to the build besides throwing warnings
            # about their 'long double' (aka u128) not being FFI safe.
            # We block those functions so that the build doesn't
            # spam warnings.
            #
            # https://github.com/rust-lang/rust-bindgen/issues/1549 describes the current problem
            # and other folks' solutions.
            #
            # We will explore migrating to https://github.com/rust-lang/rust-bindgen/pull/2122
            # when it lands
            --blocklist-function="strtold"
            --blocklist-function="qecvt"
            --blocklist-function="qecvt_r"
            --blocklist-function="qgcvt"
            --blocklist-function="qfcvt"
            --blocklist-function="qfcvt_r"
            -- # these are LLVM arg passthroughs
            -I../include
            # https://doc.rust-lang.org/nightly/rustc/platform-support.html
            --target=${RUST_BINDINGS})

set(INCLUDES "include!(\"wrapper_${RUST_BINDINGS}.rs\");\n")

add_custom_target(
  bindgen_rust_${RUST_BINDINGS}
  ALL
  ${COMMAND}
  BYPRODUCTS ${WRAPPER_TARGET}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# move files into build directory
configure_file("src/lib.rs" "src/lib.rs")

if(NOT BUILD_SHARED_LIBS)
  configure_file("build.rs" "build.rs" COPYONLY)
endif()

configure_file("Cargo.toml" "Cargo.toml" COPYONLY)
