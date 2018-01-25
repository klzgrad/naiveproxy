#!/bin/sh
set -e

export TMPDIR="$PWD/tmp"
rm -rf "$TMPDIR"
mkdir -p "$TMPDIR"

if [ "$1" = debug ]; then
  out=out/Debug
  flags="
    chrome_pgo_phase=0
    is_debug=true
    is_component_build=true"
else
  out=out/Release
  flags="
    is_official_build=true
    is_chrome_branded=true
    exclude_unwind_tables=true
    enable_resource_allowlist_generation=false
    chrome_pgo_phase=2
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

  use_udev=false
  use_aura=false
  use_ozone=false
  use_gio=false
  use_platform_icu_alternatives=true
  use_glib=false
  is_perfetto_embedder=true

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
  enable_quic_proxy_support=true
  enable_disk_cache_sql_backend=false

  use_nss_certs=false

  enable_backup_ref_ptr_support=false
  enable_dangling_raw_ptr_checks=false

  use_clang_modules=false
'

if [ "$WITH_SYSROOT" ]; then
  flags="$flags
    target_sysroot=\"//$WITH_SYSROOT\""
fi

if [ "$host_os" = "mac" ]; then
  flags="$flags"'
    mac_allow_system_xcode_for_official_builds_for_testing=true
    enable_dsyms=false'
fi

case "$EXTRA_FLAGS" in
*target_os=\"android\"*)
  # default_min_sdk_version=24: 26 introduces unnecessary snew symbols
  # is_high_end_android=true: Does not optimize for size, Uses PGO profiles
  flags="$flags"'
    is_desktop_android=true
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

if [ "$host_os" = linux ]; then
  clang_x64_targets=$(grep -o ' | .*' $out/toolchain.ninja | grep -o ' clang_x64/[^ ]*' | sort -u)
  if [ "$clang_x64_targets" ]; then
    CCACHE_DIR=$PWD/.host_tool_cache ccache -z
    CCACHE_DIR=$PWD/.host_tool_cache ninja -C "$out" $clang_x64_targets
    CCACHE_DIR=$PWD/.host_tool_cache ccache -s
    if [ "$WARMUP_HOST_TOOLS" ]; then
      exit 0
    fi
  fi
fi

ninja -C "$out" naive
