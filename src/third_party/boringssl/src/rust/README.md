bssl-sys
============

A low-level binding crate for Rust that moves in lockstop with BoringSSL. BoringSSL explicitly does not have a stable ABI, `bssl-sys` is the solution for preventing subtle-memory corruption bugs due to version skew.

### How it works
`bssl-sys` uses `bindgen` as part of the cmake build process to generate Rust compatibility shims for the targeted platform. It is important to generate it for the correct platform because `bindgen` uses LLVM information for alignment which varies depending on architecture. These files are then packaged into a Rust crate.

### To Use
Build `boringssl` with `-DRUST_BINDINGS=<rust-triple>` and ensure that you have `bindgen` installed.

The `rust-triple` option should be one of the supported targets at https://doc.rust-lang.org/nightly/rustc/platform-support.html.

