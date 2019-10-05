#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

.() { :; }

source "${SCRIPT_DIR}/sysroot-creator-bullseye.sh"

unset -f .

DEBIAN_PACKAGES='
  libc6
  libc6-dev
  linux-libc-dev
  libgcc-10-dev
  libgcc-s1
  libgomp1
  libatomic1
  libstdc++-10-dev
  libstdc++6
  libcrypt1
'

DEBIAN_PACKAGES_AMD64='
  liblsan0
  libtsan0
'
DEBIAN_PACKAGES_X86='
  libasan6
  libitm1
  libquadmath0
  libubsan1
'
DEBIAN_PACKAGES_ARM='
  libasan6
  libubsan1
'
DEBIAN_PACKAGES_ARM64='
  libasan6
  libgmp10
  libitm1
  liblsan0
  libtsan0
  libubsan1
'
DEBIAN_PACKAGES_MIPS64EL='
'

cp() {
  [ "${2##*/}" = symbols ] && return
  /bin/cp "$@"
}
rm() {
  [ "${1##*/}" = default.conf ] && return
  /bin/rm "$@"
}
mv() {
  [ "${2##*/}" = pkgconfig ] && return
  /bin/mv "$@"
}
tar() {
  echo tar "$@"
}

. "${SCRIPT_DIR}/sysroot-creator.sh"
