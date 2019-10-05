#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

.() { :; }

source "${SCRIPT_DIR}/sysroot-creator-sid.sh"

unset -f .

DEBIAN_PACKAGES='
  libc6
  libc6-dev
  linux-libc-dev
  libgcc-6-dev
  libgcc1
  libgomp1
  libatomic1
  libstdc++6
  libnspr4
  libnspr4-dev
  libnss3
  libnss3-dev
  libsqlite3-0
'

DEBIAN_PACKAGES_AMD64='
  liblsan0
  libtsan0
'
DEBIAN_PACKAGES_X86='
  libasan3
  libcilkrts5
  libitm1
  libmpx2
  libquadmath0
  libubsan0
'
DEBIAN_PACKAGES_ARM='
  libasan3
  libubsan0
'
DEBIAN_PACKAGES_ARM64='
  libasan3
  libitm1
  libubsan0
'
DEBIAN_PACKAGES_MIPS64EL='
'

# Disables libdbus workarounds
ln -sf /bin/true strip
ln -sf /bin/true arm-linux-gnueabihf-strip
ln -sf /bin/true mipsel-linux-gnu-strip
ln -sf /bin/true mips64el-linux-gnuabi64-strip
export PATH="$PWD:$PATH"
cp() {
  [ "${1##*/}" = libdbus-1-3-symbols ] && return
  /bin/cp "$@"
}
tar() {
  echo tar "$@"
}

trap "cd $PWD; rm strip *-strip" EXIT

. "${SCRIPT_DIR}/sysroot-creator.sh"
