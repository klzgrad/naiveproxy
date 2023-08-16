bssl-crypto
============

Rust bindings to BoringSSL which wrap bssl-sys. Before using this crate, first [set up `bssl-sys`](../bssl-sys/README.md).

Then to run all tests:
```
cd rust/bssl-crypto && cargo clippy && cargo deny check && cargo test
```

Unlike BoringSSL itself, this crate does not attempt to handle allocation failures. If an allocation fails, functions in this crate will panic.
