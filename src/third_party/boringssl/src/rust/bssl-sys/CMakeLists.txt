# Additional interop for things like macros and inlined functions.
add_library(rust_wrapper STATIC rust_wrapper.c)
target_link_libraries(rust_wrapper crypto)


# Generate architecture-specific wrappers.
set(WRAPPER_TARGET ${CMAKE_BINARY_DIR}/rust/bssl-sys/src/wrapper_${RUST_BINDINGS}.rs)
set(COMMAND ${BINDGEN_EXECUTABLE} "wrapper.h"
            -o ${WRAPPER_TARGET}
            --no-derive-default
            --enable-function-attribute-detection
            --use-core
            --size_t-is-usize
            --default-macro-constant-type="signed"
            --rustified-enum="point_conversion_form_t"
            --allowlist-file=".*/include/openssl/.*\\.h"
            --allowlist-file=".*/rust_wrapper\\.h"
            -- # these are LLVM arg passthroughs
            -I../../include
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