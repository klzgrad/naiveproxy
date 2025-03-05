// Copyright 2021 The BoringSSL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use std::env;
use std::path::Path;
use std::path::PathBuf;

// Keep in sync with the list in include/openssl/opensslconf.h
const OSSL_CONF_DEFINES: &[&str] = &[
    "OPENSSL_NO_ASYNC",
    "OPENSSL_NO_BF",
    "OPENSSL_NO_BLAKE2",
    "OPENSSL_NO_BUF_FREELISTS",
    "OPENSSL_NO_CAMELLIA",
    "OPENSSL_NO_CAPIENG",
    "OPENSSL_NO_CAST",
    "OPENSSL_NO_CMS",
    "OPENSSL_NO_COMP",
    "OPENSSL_NO_CT",
    "OPENSSL_NO_DANE",
    "OPENSSL_NO_DEPRECATED",
    "OPENSSL_NO_DGRAM",
    "OPENSSL_NO_DYNAMIC_ENGINE",
    "OPENSSL_NO_EC_NISTP_64_GCC_128",
    "OPENSSL_NO_EC2M",
    "OPENSSL_NO_EGD",
    "OPENSSL_NO_ENGINE",
    "OPENSSL_NO_GMP",
    "OPENSSL_NO_GOST",
    "OPENSSL_NO_HEARTBEATS",
    "OPENSSL_NO_HW",
    "OPENSSL_NO_IDEA",
    "OPENSSL_NO_JPAKE",
    "OPENSSL_NO_KRB5",
    "OPENSSL_NO_MD2",
    "OPENSSL_NO_MDC2",
    "OPENSSL_NO_OCB",
    "OPENSSL_NO_OCSP",
    "OPENSSL_NO_RC2",
    "OPENSSL_NO_RC5",
    "OPENSSL_NO_RFC3779",
    "OPENSSL_NO_RIPEMD",
    "OPENSSL_NO_RMD160",
    "OPENSSL_NO_SCTP",
    "OPENSSL_NO_SEED",
    "OPENSSL_NO_SM2",
    "OPENSSL_NO_SM3",
    "OPENSSL_NO_SM4",
    "OPENSSL_NO_SRP",
    "OPENSSL_NO_SSL_TRACE",
    "OPENSSL_NO_SSL2",
    "OPENSSL_NO_SSL3",
    "OPENSSL_NO_SSL3_METHOD",
    "OPENSSL_NO_STATIC_ENGINE",
    "OPENSSL_NO_STORE",
    "OPENSSL_NO_WHIRLPOOL",
];

fn get_bssl_build_dir() -> PathBuf {
    println!("cargo:rerun-if-env-changed=BORINGSSL_BUILD_DIR");
    if let Some(build_dir) = env::var_os("BORINGSSL_BUILD_DIR") {
        return PathBuf::from(build_dir);
    }

    let crate_dir = env::var_os("CARGO_MANIFEST_DIR").unwrap();
    return Path::new(&crate_dir).join("../../build");
}

fn get_cpp_runtime_lib() -> Option<String> {
    println!("cargo:rerun-if-env-changed=BORINGSSL_RUST_CPPLIB");

    if let Ok(cpp_lib) = env::var("BORINGSSL_RUST_CPPLIB") {
        return Some(cpp_lib);
    }

    if env::var_os("CARGO_CFG_UNIX").is_some() {
        match env::var("CARGO_CFG_TARGET_OS").unwrap().as_ref() {
            "macos" => Some("c++".into()),
            _ => Some("stdc++".into()),
        }
    } else {
        None
    }
}

fn main() {
    let bssl_build_dir = get_bssl_build_dir();
    let bssl_sys_build_dir = bssl_build_dir.join("rust/bssl-sys");
    let target = env::var("TARGET").unwrap();
    let out_dir = env::var("OUT_DIR").unwrap();
    let bindgen_out_file = Path::new(&out_dir).join("bindgen.rs");

    // Find the bindgen generated target platform bindings file and put it into
    // OUT_DIR/bindgen.rs.
    let bindgen_source_file = bssl_sys_build_dir.join(format!("wrapper_{}.rs", target));
    std::fs::copy(&bindgen_source_file, &bindgen_out_file).expect(&format!(
        "Could not copy bindings from '{}' to '{}'",
        bindgen_source_file.display(),
        bindgen_out_file.display()
    ));
    println!("cargo:rerun-if-changed={}", bindgen_source_file.display());

    // Statically link libraries.
    println!(
        "cargo:rustc-link-search=native={}",
        bssl_build_dir.join("crypto").display()
    );
    println!("cargo:rustc-link-lib=static=crypto");

    println!(
        "cargo:rustc-link-search=native={}",
        bssl_build_dir.join("ssl").display()
    );
    println!("cargo:rustc-link-lib=static=ssl");

    println!(
        "cargo:rustc-link-search=native={}",
        bssl_sys_build_dir.display()
    );
    println!("cargo:rustc-link-lib=static=rust_wrapper");

    if let Some(cpp_lib) = get_cpp_runtime_lib() {
        println!("cargo:rustc-link-lib={}", cpp_lib);
    }

    println!("cargo:conf={}", OSSL_CONF_DEFINES.join(","));
}
