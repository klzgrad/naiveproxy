#!/usr/bin/env python3
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
This script is used to build Debian sysroot images for building Chromium.
"""

import argparse
import hashlib
import lzma
import os
import re
import shutil
import subprocess
import tempfile
import time

import requests
import reversion_glibc

DISTRO = "debian"
RELEASE = "bullseye"

# This number is appended to the sysroot key to cause full rebuilds.  It
# should be incremented when removing packages or patching existing packages.
# It should not be incremented when adding packages.
SYSROOT_RELEASE = 2

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

CHROME_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
BUILD_DIR = os.path.join(CHROME_DIR, "out", "sysroot-build", RELEASE)

# gpg keyring file generated using generate_keyring.sh
KEYRING_FILE = os.path.join(SCRIPT_DIR, "keyring.gpg")

ARCHIVE_TIMESTAMP = "20230611T210420Z"

ARCHIVE_URL = f"https://snapshot.debian.org/archive/debian/{ARCHIVE_TIMESTAMP}/"
APT_SOURCES_LIST = [
    # This mimics a sources.list from bullseye.
    ("bullseye", ["main", "contrib", "non-free"]),
    ("bullseye-updates", ["main", "contrib", "non-free"]),
    ("bullseye-backports", ["main", "contrib", "non-free"]),
]

TRIPLES = {
    "amd64": "x86_64-linux-gnu",
    "i386": "i386-linux-gnu",
    "armhf": "arm-linux-gnueabihf",
    "arm64": "aarch64-linux-gnu",
    "mipsel": "mipsel-linux-gnu",
    "mips64el": "mips64el-linux-gnuabi64",
    "riscv64": "riscv64-linux-gnu",
}

REQUIRED_TOOLS = [
    "dpkg-deb",
    "file",
    "gpgv",
    "readelf",
    "tar",
    "xz",
]

# Package configuration
PACKAGES_EXT = "xz"
RELEASE_FILE = "Release"
RELEASE_FILE_GPG = "Release.gpg"

# Packages common to all architectures.
DEBIAN_PACKAGES = [
    "libatomic1",
    "libc6",
    "libc6-dev",
    "libcrypt1",
    "libgcc-10-dev",
    "libgcc-s1",
    "libgomp1",
    "libstdc++-10-dev",
    "libstdc++6",
    "linux-libc-dev",
]

DEBIAN_PACKAGES_ARCH = {
    "amd64": [
        "libasan6",
        "libitm1",
        "liblsan0",
        "libquadmath0",
        "libtsan0",
        "libubsan1",
    ],
    "i386": [
        "libasan6",
        "libitm1",
        "libquadmath0",
        "libubsan1",
    ],
    "armhf": [
        "libasan6",
        "libubsan1",
    ],
    "arm64": [
        "libasan6",
        "libgmp10",
        "libitm1",
        "liblsan0",
        "libtsan0",
        "libubsan1",
    ],
    "mipsel": [],
    "mips64el": [],
    "riscv64": [],
}


def banner(message: str) -> None:
    print("#" * 70)
    print(message)
    print("#" * 70)


def sub_banner(message: str) -> None:
    print("-" * 70)
    print(message)
    print("-" * 70)


def hash_file(hasher, file_name: str) -> str:
    with open(file_name, "rb") as f:
        while chunk := f.read(8192):
            hasher.update(chunk)
    return hasher.hexdigest()


def atomic_copyfile(source: str, destination: str) -> None:
    dest_dir = os.path.dirname(destination)
    with tempfile.NamedTemporaryFile(mode="wb", delete=False,
                                     dir=dest_dir) as temp_file:
        temp_filename = temp_file.name
    shutil.copyfile(source, temp_filename)
    os.rename(temp_filename, destination)


def download_or_copy_non_unique_filename(url: str, dest: str) -> None:
    """
    Downloads a file from a given URL to a destination with a unique filename,
    based on the SHA-256 hash of the URL.
    """
    hash_digest = hashlib.sha256(url.encode()).hexdigest()
    unique_dest = f"{dest}.{hash_digest}"
    download_or_copy(url, unique_dest)
    atomic_copyfile(unique_dest, dest)


def download_or_copy(source: str, destination: str) -> None:
    """
    Downloads a file from the given URL or copies it from a local path to the
    specified destination.
    """
    if os.path.exists(destination):
        print(f"{destination} already in place")
        return

    if source.startswith(("http://", "https://")):
        download_file(source, destination)
    else:
        atomic_copyfile(source, destination)


def download_file(url: str, dest: str, retries=5) -> None:
    """
    Downloads a file from a URL to a specified destination with retry logic,
    directory creation, and atomic write.
    """
    print(f"Downloading from {url} -> {dest}")
    # Create directories if they don't exist
    os.makedirs(os.path.dirname(dest), exist_ok=True)

    for attempt in range(retries):
        try:
            with requests.get(url, stream=True) as response:
                response.raise_for_status()

                # Use a temporary file to write data
                with tempfile.NamedTemporaryFile(
                        mode="wb", delete=False,
                        dir=os.path.dirname(dest)) as temp_file:
                    for chunk in response.iter_content(chunk_size=8192):
                        temp_file.write(chunk)

                # Rename temporary file to destination file
                os.rename(temp_file.name, dest)
                print(f"Downloaded {dest}")
                break

        except requests.RequestException as e:
            print(f"Attempt {attempt} failed: {e}")
            # Exponential back-off
            time.sleep(2**attempt)
    else:
        raise Exception(f"Failed to download file after {retries} attempts")


def sanity_check() -> None:
    """
    Performs sanity checks to ensure the environment is correctly set up.
    """
    banner("Sanity Checks")

    # Determine the Chrome build directory
    os.makedirs(BUILD_DIR, exist_ok=True)
    print(f"Using build directory: {BUILD_DIR}")

    # Check for required tools
    missing = [tool for tool in REQUIRED_TOOLS if not shutil.which(tool)]
    if missing:
        raise Exception(f"Required tools not found: {', '.join(missing)}")


def clear_install_dir(install_root: str) -> None:
    if os.path.exists(install_root):
        shutil.rmtree(install_root)
    os.makedirs(install_root)


def create_tarball(install_root: str, arch: str) -> None:
    tarball_path = os.path.join(BUILD_DIR,
                                f"{DISTRO}_{RELEASE}_{arch}_sysroot.tar.xz")
    banner("Creating tarball " + tarball_path)
    command = [
        "tar",
        "-I",
        "xz -z9 -T0 --lzma2='dict=256MiB'",
        "-cf",
        tarball_path,
        "-C",
        install_root,
        ".",
    ]
    subprocess.run(command, check=True)


def generate_package_list_dist_repo(arch: str, dist: str,
                                    repo_name: str) -> list[dict[str, str]]:
    repo_basedir = f"{ARCHIVE_URL}/dists/{dist}"
    package_list = f"{BUILD_DIR}/Packages.{dist}_{repo_name}_{arch}"
    package_list = f"{package_list}.{PACKAGES_EXT}"
    package_file_arch = f"{repo_name}/binary-{arch}/Packages.{PACKAGES_EXT}"
    package_list_arch = f"{repo_basedir}/{package_file_arch}"

    download_or_copy_non_unique_filename(package_list_arch, package_list)
    verify_package_listing(package_file_arch, package_list, dist)

    # `not line.endswith(":")` is added here to handle the case of
    # "X-Cargo-Built-Using:\n rust-adler (= 1.0.2-2), ..."
    with lzma.open(package_list, "rt") as src:
        return [
            dict(
                line.split(": ", 1) for line in package_meta.splitlines()
                if not line.startswith(" ") and not line.endswith(":"))
            for package_meta in src.read().split("\n\n") if package_meta
        ]


def generate_package_list(arch: str) -> dict[str, str]:
    package_meta = {}
    for dist, repos in APT_SOURCES_LIST:
        for repo_name in repos:
            for meta in generate_package_list_dist_repo(arch, dist, repo_name):
                package_meta[meta["Package"]] = meta

    # Read the input file and create a dictionary mapping package names to URLs
    # and checksums.
    missing = set(DEBIAN_PACKAGES + DEBIAN_PACKAGES_ARCH[arch])
    package_dict: dict[str, str] = {}
    for meta in package_meta.values():
        package = meta["Package"]
        if package in missing:
            missing.remove(package)
            url = ARCHIVE_URL + meta["Filename"]
            package_dict[url] = meta["SHA256"]
    if missing:
        raise Exception(f"Missing packages: {', '.join(missing)}")

    # Write the URLs and checksums of the requested packages to the output file
    output_file = os.path.join(SCRIPT_DIR, "generated_package_lists",
                               f"{RELEASE}.{arch}")
    with open(output_file, "w") as f:
        f.write("\n".join(sorted(package_dict)) + "\n")
    return package_dict


def hacks_and_patches(install_root: str, script_dir: str, arch: str) -> None:
    banner("Misc Hacks & Patches")

    # Remove an unnecessary dependency on qtchooser.
    qtchooser_conf = os.path.join(install_root, "usr", "lib", TRIPLES[arch],
                                  "qt-default/qtchooser/default.conf")
    if os.path.exists(qtchooser_conf):
        os.remove(qtchooser_conf)

    # __GLIBC_MINOR__ is used as a feature test macro. Replace it with the
    # earliest supported version of glibc (2.26).
    features_h = os.path.join(install_root, "usr", "include", "features.h")
    replace_in_file(features_h, r"(#define\s+__GLIBC_MINOR__)", r"\1 26 //")

    # fcntl64() was introduced in glibc 2.28. Make sure to use fcntl() instead.
    fcntl_h = os.path.join(install_root, "usr", "include", "fcntl.h")
    replace_in_file(
        fcntl_h,
        r"#ifndef __USE_FILE_OFFSET64(\nextern int fcntl)",
        r"#if 1\1",
    )

    # Do not use pthread_cond_clockwait as it was introduced in glibc 2.30.
    cppconfig_h = os.path.join(
        install_root,
        "usr",
        "include",
        TRIPLES[arch],
        "c++",
        "10",
        "bits",
        "c++config.h",
    )
    replace_in_file(cppconfig_h,
                    r"(#define\s+_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT)",
                    r"// \1")

    # Include limits.h in stdlib.h to fix an ODR issue.
    stdlib_h = os.path.join(install_root, "usr", "include", "stdlib.h")
    replace_in_file(stdlib_h, r"(#include <stddef.h>)",
                    r"\1\n#include <limits.h>")

    # Move pkgconfig scripts.
    pkgconfig_dir = os.path.join(install_root, "usr", "lib", "pkgconfig")
    os.makedirs(pkgconfig_dir, exist_ok=True)
    triple_pkgconfig_dir = os.path.join(install_root, "usr", "lib",
                                        TRIPLES[arch], "pkgconfig")
    if os.path.exists(triple_pkgconfig_dir):
        for file in os.listdir(triple_pkgconfig_dir):
            shutil.move(os.path.join(triple_pkgconfig_dir, file),
                        pkgconfig_dir)

    # Avoid requiring unsupported glibc versions.
    for lib in ["libc.so.6", "libm.so.6", "libcrypt.so.1"]:
        # RISCV64 is new and has no backward compatibility.
        # Reversioning would remove necessary symbols and cause linking failures.
        if arch == "riscv64":
            continue
        lib_path = os.path.join(install_root, "lib", TRIPLES[arch], lib)
        reversion_glibc.reversion_glibc(lib_path)


def replace_in_file(file_path: str, search_pattern: str,
                    replace_pattern: str) -> None:
    with open(file_path, "r") as file:
        content = file.read()
    with open(file_path, "w") as file:
        file.write(re.sub(search_pattern, replace_pattern, content))


def install_into_sysroot(build_dir: str, install_root: str,
                         packages: dict[str, str]) -> None:
    """
    Installs libraries and headers into the sysroot environment.
    """
    banner("Install Libs And Headers Into Jail")

    debian_packages_dir = os.path.join(build_dir, "debian-packages")
    os.makedirs(debian_packages_dir, exist_ok=True)

    debian_dir = os.path.join(install_root, "debian")
    os.makedirs(debian_dir, exist_ok=True)
    control_file = os.path.join(debian_dir, "control")
    # Create an empty control file
    open(control_file, "a").close()

    for package, sha256sum in packages.items():
        package_name = os.path.basename(package)
        package_path = os.path.join(debian_packages_dir, package_name)

        banner(f"Installing {package_name}")
        download_or_copy(package, package_path)
        if hash_file(hashlib.sha256(), package_path) != sha256sum:
            raise ValueError(f"SHA256 mismatch for {package_path}")

        sub_banner(f"Extracting to {install_root}")
        subprocess.run(["dpkg-deb", "-x", package_path, install_root],
                       check=True)

        base_package = get_base_package_name(package_path)
        debian_package_dir = os.path.join(debian_dir, base_package, "DEBIAN")

        # Extract the control file
        os.makedirs(debian_package_dir, exist_ok=True)
        with subprocess.Popen(
            ["dpkg-deb", "-e", package_path, debian_package_dir],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
        ) as proc:
            _, err = proc.communicate()
            if proc.returncode != 0:
                message = "Failed to extract control from"
                raise Exception(
                    f"{message} {package_path}: {err.decode('utf-8')}")

    # Prune /usr/share, leaving only pkgconfig, wayland, and wayland-protocols
    usr_share = os.path.join(install_root, "usr", "share")
    for item in os.listdir(usr_share):
        full_path = os.path.join(usr_share, item)
        if os.path.isdir(full_path) and item not in [
                "pkgconfig",
                "wayland",
                "wayland-protocols",
        ]:
            shutil.rmtree(full_path)


def get_base_package_name(package_path: str) -> str:
    """
    Retrieves the base package name from a Debian package.
    """
    result = subprocess.run(["dpkg-deb", "--field", package_path, "Package"],
                            capture_output=True,
                            text=True)
    if result.returncode != 0:
        raise Exception(
            f"Failed to get package name from {package_path}: {result.stderr}")
    return result.stdout.strip()


def cleanup_jail_symlinks(install_root: str) -> None:
    """
    Cleans up jail symbolic links by converting absolute symlinks
    into relative ones.
    """
    for root, dirs, files in os.walk(install_root):
        for name in files + dirs:
            full_path = os.path.join(root, name)
            if os.path.islink(full_path):
                target_path = os.readlink(full_path)

                # Check if the symlink is absolute and points inside the
                # install_root.
                if os.path.isabs(target_path):
                    # Compute the relative path from the symlink to the target.
                    relative_path = os.path.relpath(
                        os.path.join(install_root, target_path.strip("/")),
                        os.path.dirname(full_path),
                    )
                    # Verify that the target exists inside the install_root.
                    joined_path = os.path.join(os.path.dirname(full_path),
                                               relative_path)
                    if not os.path.exists(joined_path):
                        raise Exception(
                            f"Link target doesn't exist: {joined_path}")
                    os.remove(full_path)
                    os.symlink(relative_path, full_path)


def verify_library_deps(install_root: str) -> None:
    """
    Verifies if all required libraries are present in the sysroot environment.
    """
    # Get all shared libraries and their dependencies.
    shared_libs = set()
    needed_libs = set()
    for root, _, files in os.walk(install_root):
        for file in files:
            if ".so" not in file:
                continue
            path = os.path.join(root, file)
            islink = os.path.islink(path)
            if islink:
                path = os.path.join(root, os.readlink(path))
            cmd_file = ["file", path]
            output = subprocess.check_output(cmd_file).decode()
            if ": ELF" not in output or "shared object" not in output:
                continue
            shared_libs.add(file)
            if islink:
                continue
            cmd_readelf = ["readelf", "-d", path]
            output = subprocess.check_output(cmd_readelf).decode()
            for line in output.split("\n"):
                if "NEEDED" in line:
                    needed_libs.add(line.split("[")[1].split("]")[0])

    missing_libs = needed_libs - shared_libs
    if missing_libs:
        raise Exception(f"Missing libraries: {missing_libs}")


def build_sysroot(arch: str) -> None:
    install_root = os.path.join(BUILD_DIR, f"{RELEASE}_{arch}_staging")
    clear_install_dir(install_root)
    packages = generate_package_list(arch)
    install_into_sysroot(BUILD_DIR, install_root, packages)
    hacks_and_patches(install_root, SCRIPT_DIR, arch)
    cleanup_jail_symlinks(install_root)
    verify_library_deps(install_root)


def upload_sysroot(arch: str) -> str:
    tarball_path = os.path.join(BUILD_DIR,
                                f"{DISTRO}_{RELEASE}_{arch}_sysroot.tar.xz")
    command = [
        "upload_to_google_storage_first_class.py",
        "--bucket",
        "chrome-linux-sysroot",
        tarball_path,
    ]
    return subprocess.check_output(command).decode("utf-8")


def verify_package_listing(file_path: str, output_file: str,
                           dist: str) -> None:
    """
    Verifies the downloaded Packages.xz file against its checksum and GPG keys.
    """
    # Paths for Release and Release.gpg files
    repo_basedir = f"{ARCHIVE_URL}/dists/{dist}"
    release_list = f"{repo_basedir}/{RELEASE_FILE}"
    release_list_gpg = f"{repo_basedir}/{RELEASE_FILE_GPG}"

    release_file = os.path.join(BUILD_DIR, f"{dist}-{RELEASE_FILE}")
    release_file_gpg = os.path.join(BUILD_DIR, f"{dist}-{RELEASE_FILE_GPG}")

    if not os.path.exists(KEYRING_FILE):
        raise Exception(f"KEYRING_FILE not found: {KEYRING_FILE}")

    # Download Release and Release.gpg files
    download_or_copy_non_unique_filename(release_list, release_file)
    download_or_copy_non_unique_filename(release_list_gpg, release_file_gpg)

    # Verify Release file with GPG
    subprocess.run(
        ["gpgv", "--keyring", KEYRING_FILE, release_file_gpg, release_file],
        check=True)

    # Find the SHA256 checksum for the specific file in the Release file
    sha256sum_pattern = re.compile(r"([a-f0-9]{64})\s+\d+\s+" +
                                   re.escape(file_path) + r"$")
    sha256sum_match = None
    with open(release_file, "r") as f:
        for line in f:
            if match := sha256sum_pattern.search(line):
                sha256sum_match = match.group(1)
                break

    if not sha256sum_match:
        raise Exception(
            f"Checksum for {file_path} not found in {release_file}")

    if hash_file(hashlib.sha256(), output_file) != sha256sum_match:
        raise Exception(f"Checksum mismatch for {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description="Build and upload Debian sysroot images for Chromium.")
    parser.add_argument("command", choices=["build", "upload"])
    parser.add_argument("architecture", choices=list(TRIPLES))
    args = parser.parse_args()

    sanity_check()

    # RISCV64 only has support in debian-ports, no support in bookworm.
    if args.architecture == "riscv64":
        global ARCHIVE_URL, APT_SOURCES_LIST
        ARCHIVE_URL = "https://snapshot.debian.org/archive/debian-ports/20230724T141507Z/"
        APT_SOURCES_LIST = [("sid", ["main"])]

    if args.command == "build":
        build_sysroot(args.architecture)
    elif args.command == "upload":
        upload_sysroot(args.architecture)


if __name__ == "__main__":
    main()
