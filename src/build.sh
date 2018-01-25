#!/bin/sh
set -e

export TMPDIR="$PWD/tmp"
rm -rf "$TMPDIR"
mkdir -p "$TMPDIR"

if [ "$1" = debug ]; then
  out=out/Debug
  flags="
    is_debug=true
    is_component_build=true"
else
  out=out/Release
  flags="
    is_official_build=true
    exclude_unwind_tables=true
    enable_resource_allowlist_generation=false
    symbol_level=0"
fi

. ./get-sysroot.sh

if [ "$CCACHE" ]; then
  flags="$flags
    cc_wrapper=\"$CCACHE\""
fi

flags="$flags"'
  is_clang=true
  use_sysroot=false

  fatal_linker_warnings=false
  treat_warnings_as_errors=false

  enable_base_tracing=false
  use_udev=false
  use_aura=false
  use_ozone=false
  use_gio=false
  use_gtk=false
  use_platform_icu_alternatives=true
  use_glib=false

  disable_file_support=true
  enable_websockets=false
  use_kerberos=false
  enable_mdns=false
  enable_reporting=false
  include_transport_security_state_preload_list=false
  use_nss_certs=false
'

if [ "$WITH_SYSROOT" ]; then
  flags="$flags
    target_sysroot=\"//$WITH_SYSROOT\""
fi

if [ "$USE_AFDO" ]; then
  flags="$flags"'
    clang_sample_profile_path="//chrome/android/profiles/afdo.prof"'
fi

if [ "$ARCH" = "Darwin" ]; then
  flags="$flags"'
    enable_dsyms=false'
fi

if [ "$target_cpu" = "mipsel" -o "$target_cpu" = "mips64el" ]; then
  flags="$flags"'
    use_thin_lto=false
    chrome_pgo_phase=0'
fi

rm -rf "./$out"
mkdir -p out

export DEPOT_TOOLS_WIN_TOOLCHAIN=0

./gn/out/gn gen "$out" --args="$flags $EXTRA_FLAGS" --script-executable=$PYTHON

ninja -C "$out" naive
