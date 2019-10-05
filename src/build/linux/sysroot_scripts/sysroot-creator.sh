#!/bin/bash

# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#@ This script builds Debian sysroot images for building Google Chrome.
#@
#@  Usage:
#@    sysroot-creator.sh {build,upload} \
#@    {amd64,i386,armhf,arm64,armel,mipsel,mips64el}
#@

######################################################################
# Config
######################################################################

set -o nounset
set -o errexit

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

DISTRO=debian
RELEASE=bullseye

# This number is appended to the sysroot key to cause full rebuilds.  It
# should be incremented when removing packages or patching existing packages.
# It should not be incremented when adding packages.
SYSROOT_RELEASE=2

ARCHIVE_TIMESTAMP=20230611T210420Z

ARCHIVE_URL="https://snapshot.debian.org/archive/debian/$ARCHIVE_TIMESTAMP/"
APT_SOURCES_LIST=(
  # This mimics a sources.list from bullseye.
  "${ARCHIVE_URL} bullseye main contrib non-free"
  "${ARCHIVE_URL} bullseye-updates main contrib non-free"
  "${ARCHIVE_URL} bullseye-backports main contrib non-free"
)

# gpg keyring file generated using generate_keyring.sh
KEYRING_FILE="${SCRIPT_DIR}/keyring.gpg"

# Sysroot packages: these are the packages needed to build chrome.
DEBIAN_PACKAGES="\
  libatomic1
  libc6
  libc6-dev
  libcrypt1
  libgcc-10-dev
  libgcc-s1
  libgomp1
  libstdc++-10-dev
  libstdc++6
  linux-libc-dev
"

DEBIAN_PACKAGES_AMD64="
  libasan6
  libitm1
  liblsan0
  libquadmath0
  libtsan0
  libubsan1
"

DEBIAN_PACKAGES_I386="
  libasan6
  libitm1
  libquadmath0
  libubsan1
"

DEBIAN_PACKAGES_ARMHF="
  libasan6
  libubsan1
"

DEBIAN_PACKAGES_ARM64="
  libasan6
  libgmp10
  libitm1
  liblsan0
  libtsan0
  libubsan1
"

DEBIAN_PACKAGES_ARMEL="
  libasan6
  libubsan1
"

DEBIAN_PACKAGES_MIPSEL="
"

DEBIAN_PACKAGES_MIPS64EL="
"

DEBIAN_PACKAGES_RISCV64="
"

readonly REQUIRED_TOOLS="curl xzcat"

######################################################################
# Package Config
######################################################################

readonly PACKAGES_EXT=xz
readonly RELEASE_FILE="Release"
readonly RELEASE_FILE_GPG="Release.gpg"

######################################################################
# Helper
######################################################################

Banner() {
  echo "######################################################################"
  echo $*
  echo "######################################################################"
}


SubBanner() {
  echo "----------------------------------------------------------------------"
  echo $*
  echo "----------------------------------------------------------------------"
}


Usage() {
  egrep "^#@" "${BASH_SOURCE[0]}" | cut --bytes=3-
}


DownloadOrCopyNonUniqueFilename() {
  # Use this function instead of DownloadOrCopy when the url uniquely
  # identifies the file, but the filename (excluding the directory)
  # does not.
  local url="$1"
  local dest="$2"

  local hash="$(echo "$url" | sha256sum | cut -d' ' -f1)"

  DownloadOrCopy "${url}" "${dest}.${hash}"
  # cp the file to prevent having to redownload it, but mv it to the
  # final location so that it's atomic.
  cp "${dest}.${hash}" "${dest}.$$"
  mv "${dest}.$$" "${dest}"
}

DownloadOrCopy() {
  if [ -f "$2" ] ; then
    echo "$2 already in place"
    return
  fi

  HTTP=0
  echo "$1" | grep -Eqs '^https?://' && HTTP=1
  if [ "$HTTP" = "1" ]; then
    SubBanner "downloading from $1 -> $2"
    # Appending the "$$" shell pid is necessary here to prevent concurrent
    # instances of sysroot-creator.sh from trying to write to the same file.
    local temp_file="${2}.partial.$$"
    # curl --retry doesn't retry when the page gives a 4XX error, so we need to
    # manually rerun.
    for i in {1..10}; do
      # --create-dirs is added in case there are slashes in the filename, as can
      # happen with the "debian/security" release class.
      local http_code=$(curl -L "$1" --create-dirs -o "${temp_file}" \
                        -w "%{http_code}")
      if [ ${http_code} -eq 200 ]; then
        break
      fi
      echo "Bad HTTP code ${http_code} when downloading $1"
      rm -f "${temp_file}"
      sleep $i
    done
    if [ ! -f "${temp_file}" ]; then
      exit 1
    fi
    mv "${temp_file}" $2
  else
    SubBanner "copying from $1"
    cp "$1" "$2"
  fi
}

SetEnvironmentVariables() {
  case $ARCH in
    amd64)
      TRIPLE=x86_64-linux-gnu
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_AMD64}"
      ;;
    i386)
      TRIPLE=i386-linux-gnu
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_I386}"
      ;;
    armhf)
      TRIPLE=arm-linux-gnueabihf
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_ARMHF}"
      ;;
    arm64)
      TRIPLE=aarch64-linux-gnu
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_ARM64}"
      ;;
    armel)
      TRIPLE=arm-linux-gnueabi
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_ARMEL}"
      ;;
    mipsel)
      TRIPLE=mipsel-linux-gnu
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_MIPSEL}"
      ;;
    mips64el)
      TRIPLE=mips64el-linux-gnuabi64
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_MIPS64EL}"
      ;;
    riscv64)
      TRIPLE=riscv64-linux-gnu
      DEBIAN_PACKAGES_ARCH="${DEBIAN_PACKAGES_RISCV64}"
      # RISCV64 has no support in bookworm
      APT_SOURCES_LIST=("https://snapshot.debian.org/archive/debian-ports/20230724T141507Z/ sid main")
      ;;
    *)
      echo "ERROR: Unsupported architecture: $ARCH"
      Usage
      exit 1
      ;;
  esac
}

# some sanity checks to make sure this script is run from the right place
# with the right tools
SanityCheck() {
  Banner "Sanity Checks"

  local chrome_dir=$(cd "${SCRIPT_DIR}/../../.." && pwd)
  BUILD_DIR="${chrome_dir}/out/sysroot-build/${RELEASE}"
  mkdir -p ${BUILD_DIR}
  echo "Using build directory: ${BUILD_DIR}"

  for tool in ${REQUIRED_TOOLS} ; do
    if ! which ${tool} > /dev/null ; then
      echo "Required binary $tool not found."
      echo "Exiting."
      exit 1
    fi
  done

  # This is where the staging sysroot is.
  INSTALL_ROOT="${BUILD_DIR}/${RELEASE}_${ARCH}_staging"
  TARBALL="${BUILD_DIR}/${DISTRO}_${RELEASE}_${ARCH}_sysroot.tar.xz"

  if ! mkdir -p "${INSTALL_ROOT}" ; then
    echo "ERROR: ${INSTALL_ROOT} can't be created."
    exit 1
  fi
}


ChangeDirectory() {
  # Change directory to where this script is.
  cd ${SCRIPT_DIR}
}


ClearInstallDir() {
  Banner "Clearing dirs in ${INSTALL_ROOT}"
  rm -rf ${INSTALL_ROOT}/*
}


CreateTarBall() {
  Banner "Creating tarball ${TARBALL}"
  tar -I "xz -9 -T0" -cf ${TARBALL} -C ${INSTALL_ROOT} .
}

ExtractPackageXz() {
  local src_file="$1"
  local dst_file="$2"
  local repo="$3"
  xzcat "${src_file}" | egrep '^(Package:|Filename:|SHA256:) ' |
    sed "s|Filename: |Filename: ${repo}|" > "${dst_file}"
}

GeneratePackageListDistRepo() {
  local arch="$1"
  local repo="$2"
  local dist="$3"
  local repo_name="$4"

  local tmp_package_list="${BUILD_DIR}/Packages.${dist}_${repo_name}_${arch}"
  local repo_basedir="${repo}/dists/${dist}"
  local package_list="${BUILD_DIR}/Packages.${dist}_${repo_name}_${arch}.${PACKAGES_EXT}"
  local package_file_arch="${repo_name}/binary-${arch}/Packages.${PACKAGES_EXT}"
  local package_list_arch="${repo_basedir}/${package_file_arch}"

  DownloadOrCopyNonUniqueFilename "${package_list_arch}" "${package_list}"
  VerifyPackageListing "${package_file_arch}" "${package_list}" ${repo} ${dist}
  ExtractPackageXz "${package_list}" "${tmp_package_list}" ${repo}
  cat "${tmp_package_list}" | ./merge-package-lists.py "${list_base}"
}

GeneratePackageListDist() {
  local arch="$1"
  set -- $2
  local repo="$1"
  local dist="$2"
  shift 2
  while (( "$#" )); do
    GeneratePackageListDistRepo "$arch" "$repo" "$dist" "$1"
    shift
  done
}

GeneratePackageList() {
  local output_file="$1"
  local arch="$2"
  local packages="$3"

  local list_base="${BUILD_DIR}/Packages.${RELEASE}_${arch}"
  > "${list_base}"  # Create (or truncate) a zero-length file.
  printf '%s\n' "${APT_SOURCES_LIST[@]}" | while read source; do
    GeneratePackageListDist "${arch}" "${source}"
  done

  GeneratePackageListImpl "${list_base}" "${output_file}" \
    "${DEBIAN_PACKAGES} ${packages}"
}

StripChecksumsFromPackageList() {
  local package_file="$1"
  sed -i 's/ [a-f0-9]\{64\}$//' "$package_file"
}

######################################################################
#
######################################################################

HacksAndPatches() {
  Banner "Misc Hacks & Patches"

  # __GLIBC_MINOR__ is used as a feature test macro.  Replace it with the
  # earliest supported version of glibc (2.26, obtained from the oldest glibc
  # version in //chrome/installer/linux/debian/dist_packag_versions.json and
  # //chrome/installer/linux/rpm/dist_package_provides.json).
  local usr_include="${INSTALL_ROOT}/usr/include"
  local features_h="${usr_include}/features.h"
  sed -i 's|\(#define\s\+__GLIBC_MINOR__\)|\1 26 //|' "${features_h}"

  # fcntl64() was introduced in glibc 2.28.  Make sure to use fcntl() instead.
  local fcntl_h="${INSTALL_ROOT}/usr/include/fcntl.h"
  sed -i '{N; s/#ifndef __USE_FILE_OFFSET64\(\nextern int fcntl\)/#if 1\1/}' \
      "${fcntl_h}"

  # Do not use pthread_cond_clockwait as it was introduced in glibc 2.30.
  local cppconfig_h="${usr_include}/${TRIPLE}/c++/10/bits/c++config.h"
  sed -i 's|\(#define\s\+_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT\)|// \1|' \
    "${cppconfig_h}"

  # Include limits.h in stdlib.h to fix an ODR issue
  # (https://sourceware.org/bugzilla/show_bug.cgi?id=30516)
  local stdlib_h="${usr_include}/stdlib.h"
  sed -i '/#include <stddef.h>/a #include <limits.h>' "${stdlib_h}"

  # RISCV64 is new and has no backward compatibility.
  # Reversioning would remove necessary symbols and cause linking failures.
  if [ "$ARCH" = "riscv64" ]; then
    return
  fi

  # Avoid requiring unsupported glibc versions.
  "${SCRIPT_DIR}/reversion_glibc.py" \
    "${INSTALL_ROOT}/lib/${TRIPLE}/libc.so.6"
  "${SCRIPT_DIR}/reversion_glibc.py" \
    "${INSTALL_ROOT}/lib/${TRIPLE}/libm.so.6"
  "${SCRIPT_DIR}/reversion_glibc.py" \
    "${INSTALL_ROOT}/lib/${TRIPLE}/libcrypt.so.1"
}

InstallIntoSysroot() {
  Banner "Install Libs And Headers Into Jail"

  mkdir -p ${BUILD_DIR}/debian-packages
  # The /debian directory is an implementation detail that's used to cd into
  # when running dpkg-shlibdeps.
  mkdir -p ${INSTALL_ROOT}/debian
  # An empty control file is necessary to run dpkg-shlibdeps.
  touch ${INSTALL_ROOT}/debian/control
  while (( "$#" )); do
    local file="$1"
    local package="${BUILD_DIR}/debian-packages/${file##*/}"
    shift
    local sha256sum="$1"
    shift
    if [ "${#sha256sum}" -ne "64" ]; then
      echo "Bad sha256sum from package list"
      exit 1
    fi

    Banner "Installing $(basename ${file})"
    DownloadOrCopy ${file} ${package}
    if [ ! -s "${package}" ] ; then
      echo
      echo "ERROR: bad package ${package}"
      exit 1
    fi
    echo "${sha256sum}  ${package}" | sha256sum --quiet -c

    SubBanner "Extracting to ${INSTALL_ROOT}"
    dpkg-deb -x ${package} ${INSTALL_ROOT}

    base_package=$(dpkg-deb --field ${package} Package)
    mkdir -p ${INSTALL_ROOT}/debian/${base_package}/DEBIAN
    dpkg-deb -e ${package} ${INSTALL_ROOT}/debian/${base_package}/DEBIAN
  done

  # Prune /usr/share, leaving only pkgconfig, wayland, and wayland-protocols.
  ls -d ${INSTALL_ROOT}/usr/share/* | \
    grep -v "/\(pkgconfig\|wayland\|wayland-protocols\)$" | xargs rm -r
}


CleanupJailSymlinks() {
  Banner "Jail symlink cleanup"

  SAVEDPWD=$(pwd)
  cd ${INSTALL_ROOT}
  local libdirs="lib usr/lib"
  if [ -d lib64 ]; then
    libdirs="${libdirs} lib64"
  fi

  find $libdirs -type l -printf '%p %l\n' | while read link target; do
    # skip links with non-absolute paths
    echo "${target}" | grep -qs ^/ || continue
    echo "${link}: ${target}"
    # Relativize the symlink.
    prefix=$(echo "${link}" | sed -e 's/[^/]//g' | sed -e 's|/|../|g')
    ln -snfv "${prefix}${target}" "${link}"
  done

  failed=0
  while read link target; do
    # Make sure we catch new bad links.
    if [ ! -r "${link}" ]; then
      echo "ERROR: FOUND BAD LINK ${link}"
      ls -l ${link}
      failed=1
    fi
  done < <(find $libdirs -type l -printf '%p %l\n')
  if [ $failed -eq 1 ]; then
      exit 1
  fi
  cd "$SAVEDPWD"
}


VerifyLibraryDeps() {
  local find_dirs=(
    "${INSTALL_ROOT}/lib/"
    "${INSTALL_ROOT}/lib/${TRIPLE}/"
    "${INSTALL_ROOT}/usr/lib/${TRIPLE}/"
  )
  local needed_libs="$(
    find ${find_dirs[*]} -name "*\.so*" -type f -exec file {} \; | \
      grep ': ELF' | sed 's/^\(.*\): .*$/\1/' | xargs readelf -d | \
      grep NEEDED | sort | uniq | sed 's/^.*Shared library: \[\(.*\)\]$/\1/g')"
  local all_libs="$(find ${find_dirs[*]} -printf '%f\n')"
  # Ignore missing libdbus-1.so.0
  all_libs+="$(echo -e '\nlibdbus-1.so.0')"
  local missing_libs="$(grep -vFxf <(echo "${all_libs}") \
    <(echo "${needed_libs}"))"
  if [ ! -z "${missing_libs}" ]; then
    echo "Missing libraries:"
    echo "${missing_libs}"
    exit 1
  fi
}

BuildSysroot() {
  ClearInstallDir
  local package_file="generated_package_lists/${RELEASE}.${ARCH}"
  GeneratePackageList "${package_file}" $ARCH "${DEBIAN_PACKAGES_ARCH}"
  local files_and_sha256sums="$(cat ${package_file})"
  StripChecksumsFromPackageList "$package_file"
  InstallIntoSysroot ${files_and_sha256sums}
  HacksAndPatches
  CleanupJailSymlinks
  VerifyLibraryDeps
}

UploadSysroot() {
  local sha=$(sha1sum "${TARBALL}" | awk '{print $1;}')
  set -x
  gsutil.py cp -a public-read "${TARBALL}" \
      "gs://chrome-linux-sysroot/toolchain/$sha/"
  set +x
}

#
# CheckForDebianGPGKeyring
#
#     Make sure the Debian GPG keys exist. Otherwise print a helpful message.
#
CheckForDebianGPGKeyring() {
  if [ ! -e "$KEYRING_FILE" ]; then
    echo "KEYRING_FILE not found: ${KEYRING_FILE}"
    echo "Debian GPG keys missing. Install the debian-archive-keyring package."
    exit 1
  fi
}

#
# VerifyPackageListing
#
#     Verifies the downloaded Packages.xz file has the right checksums.
#
VerifyPackageListing() {
  local file_path="$1"
  local output_file="$2"
  local repo="$3"
  local dist="$4"

  local repo_basedir="${repo}/dists/${dist}"
  local release_list="${repo_basedir}/${RELEASE_FILE}"
  local release_list_gpg="${repo_basedir}/${RELEASE_FILE_GPG}"

  local release_file="${BUILD_DIR}/${dist}-${RELEASE_FILE}"
  local release_file_gpg="${BUILD_DIR}/${dist}-${RELEASE_FILE_GPG}"

  CheckForDebianGPGKeyring

  DownloadOrCopyNonUniqueFilename ${release_list} ${release_file}
  DownloadOrCopyNonUniqueFilename ${release_list_gpg} ${release_file_gpg}
  echo "Verifying: ${release_file} with ${release_file_gpg}"
  set -x
  gpgv --keyring "${KEYRING_FILE}" "${release_file_gpg}" "${release_file}"
  set +x

  echo "Verifying: ${output_file}"
  local sha256sum=$(grep -E "${file_path}\$|:\$" "${release_file}" | \
    grep "SHA256:" -A 1 | xargs echo | awk '{print $2;}')

  if [ "${#sha256sum}" -ne "64" ]; then
    echo "Bad sha256sum from ${release_list}"
    exit 1
  fi

  echo "${sha256sum}  ${output_file}" | sha256sum --quiet -c
}

#
# GeneratePackageListImpl
#
#     Looks up package names in ${BUILD_DIR}/Packages and write list of URLs
#     to output file.
#
GeneratePackageListImpl() {
  local input_file="$1"
  local output_file="$2"
  echo "Updating: ${output_file} from ${input_file}"
  /bin/rm -f "${output_file}"
  shift
  shift
  local failed=0
  for pkg in $@ ; do
    local pkg_full=$(grep -A 1 " ${pkg}\$" "$input_file" | \
      egrep "/pool" | sed 's/.*Filename: //')
    if [ -z "${pkg_full}" ]; then
      echo "ERROR: missing package: $pkg"
      local failed=1
    else
      local sha256sum=$(grep -A 4 " ${pkg}\$" "$input_file" | \
        grep ^SHA256: | sed 's/^SHA256: //')
      if [ "${#sha256sum}" -ne "64" ]; then
        echo "Bad sha256sum from Packages"
        local failed=1
      fi
      echo $pkg_full $sha256sum >> "$output_file"
    fi
  done
  if [ $failed -eq 1 ]; then
    exit 1
  fi
  # sort -o does an in-place sort of this file
  sort "$output_file" -o "$output_file"
}

if [ $# -ne 2 ]; then
  Usage
  exit 1
else
  ChangeDirectory
  ARCH=$2
  SetEnvironmentVariables
  SanityCheck
  case "$1" in
    build)
      BuildSysroot
      ;;
    upload)
      UploadSysroot
      ;;
    *)
      echo "ERROR: Invalid command: $1"
      Usage
      exit 1
      ;;
  esac
fi
