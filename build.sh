#!/bin/sh
set -e

export TMPDIR="$PWD/tmp"
mkdir -p "$TMPDIR"

if [ "$1" = debug ]; then
  out=out/Debug
  flags='
    is_debug=true
    is_component_build=true
    exclude_unwind_tables=false
    remove_webcore_debug_symbols=false
    use_thin_lto=false
    use_lld=false'
else
  out=out/Release
  flags='
    is_debug=false
    is_component_build=false
    exclude_unwind_tables=true
    remove_webcore_debug_symbols=true
    use_thin_lto=true
    use_lld=true'
fi

if [ -d /usr/lib/ccache ]; then
  export PATH="/usr/lib/ccache:$PATH"
  export CCACHE_SLOPPINESS=time_macros
  export CCACHE_BASEDIR="$PWD"
  export CCACHE_CPP2=yes
  flags="$flags"'
   cc_wrapper="ccache"'
fi

flags="$flags"'
  is_clang=true
  linux_use_bundled_binutils=false

  fatal_linker_warnings=false
  treat_warnings_as_errors=false
  use_sysroot=false

  fieldtrial_testing_like_official_build=true

  use_ozone=true
  ozone_auto_platforms=false
  ozone_platform="headless"
  ozone_platform_headless=true
  is_desktop_linux=false

  use_cups=false
  use_dbus=false
  use_gio=false
  enable_extensions=true
  use_platform_icu_alternatives=true

  disable_file_support=true
  enable_websockets=false
  disable_ftp_support=true
  use_kerberos=false
  disable_brotli_filter=true
  enable_mdns=false
  enable_reporting=false
  include_transport_security_state_preload_list=false
'

rm -rf "./$out"
mkdir -p out

if [ ! -f out/gn ]; then
  rm -rf gn
  git clone https://gn.googlesource.com/gn
  cd gn
  CC=gcc CXX=g++ python2 build/gen.py
  CCACHE_CPP2= ninja -C out gn
  cp out/gn ../out
  cd ..
fi

./out/gn gen "$out" --args="$flags" --script-executable=/usr/bin/python2

ninja -C "$out" naive
