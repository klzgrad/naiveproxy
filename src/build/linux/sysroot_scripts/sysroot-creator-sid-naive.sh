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
  libasan3
  libubsan0
  libstdc++6
  libnspr4
  libnspr4-dev
  libnss3
  libnss3-dev
  libsqlite3-0
'

DEBIAN_PACKAGES_X86='
  libcilkrts5
  libitm1
  libmpx2
  libquadmath0
'
DEBIAN_PACKAGES_ARM='
'
DEBIAN_PACKAGES_ARM64='
  libitm1
'

# Disables libdbus workarounds
ln -sf /bin/true strip
ln -sf /bin/true arm-linux-gnueabihf-strip
export PATH="$PWD:$PATH"
cp() {
  [ "${1##*/}" = libdbus-1-3-symbols ] && return
  /bin/cp "$@"
}

trap "cd $PWD; rm strip arm-linux-gnueabihf-strip" EXIT

. "${SCRIPT_DIR}/sysroot-creator.sh"
