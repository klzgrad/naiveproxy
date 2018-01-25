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

# ccache
case "$host_os" in
  linux|mac)
    if which ccache >/dev/null 2>&1; then
      export CCACHE_SLOPPINESS=time_macros
      export CCACHE_BASEDIR="$PWD"
      export CCACHE_CPP2=yes
      CCACHE=ccache
    fi
  ;;
  win)
    if [ -f "$HOME"/.cargo/bin/sccache* ]; then
      export PATH="$PATH:$HOME/.cargo/bin"
      CCACHE=sccache
    fi
  ;;
esac
if [ "$CCACHE" ]; then
  flags="$flags
    cc_wrapper=\"$CCACHE\""
fi

flags="$flags"'
  is_clang=true
  use_sysroot=false

  fatal_linker_warnings=false
  treat_warnings_as_errors=false

  is_cronet_build=true
  chrome_pgo_phase=2

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
  disable_file_support=true
  disable_zstd_filter=false
  enable_mdns=false
  enable_reporting=false
  include_transport_security_state_preload_list=false
  enable_device_bound_sessions=false
  enable_bracketed_proxy_uris=true

  use_nss_certs=false

  enable_backup_ref_ptr_support=false
  enable_dangling_raw_ptr_checks=false
'

if [ "$WITH_SYSROOT" ]; then
  flags="$flags
    target_sysroot=\"//$WITH_SYSROOT\""
fi

if [ "$host_os" = "mac" ]; then
  flags="$flags"'
    enable_dsyms=false'
fi

case "$EXTRA_FLAGS" in
*target_os=\"android\"*)
  # default_min_sdk_version=24: 26 introduces unnecessary snew symbols
  # is_high_end_android=true: Does not optimize for size, Uses PGO profiles
  flags="$flags"'
    default_min_sdk_version=24
    is_high_end_android=true'
  ;;
esac

# See https://github.com/llvm/llvm-project/issues/86430
if [ "$target_os" = "linux" -a "$target_cpu" = "x64" ]; then
  flags="$flags"'
    use_cfi_icall=false'
fi


rm -rf "./$out"
mkdir -p out

export DEPOT_TOOLS_WIN_TOOLCHAIN=0

./gn/out/gn gen "$out" --args="$flags $EXTRA_FLAGS"

ninja -C "$out" naive
