#!/bin/sh
set -e

export TMPDIR="$PWD/tmp"
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
    enable_resource_whitelist_generation=false
    symbol_level=0"
fi

if which ccache >/dev/null 2>&1; then
  export CCACHE_SLOPPINESS=time_macros
  export CCACHE_BASEDIR="$PWD"
  export CCACHE_CPP2=yes
  flags="$flags"'
   cc_wrapper="ccache"'
elif [ -f "$HOME"/.cargo/bin/sccache* ]; then
  export PATH="$PATH:$HOME/.cargo/bin"
  flags="$flags"'
   cc_wrapper="sccache"'
fi

flags="$flags"'
  is_clang=true
  use_sysroot=false

  fatal_linker_warnings=false
  treat_warnings_as_errors=false

  fieldtrial_testing_like_official_build=true

  enable_base_tracing=false
  enable_nacl=false
  enable_print_preview=false
  enable_remoting=false
  use_alsa=false
  use_cups=false
  use_dbus=false
  use_gio=false
  use_platform_icu_alternatives=true
  use_gtk=false
  use_gnome_keyring=false
  use_libpci=false
  use_pangocairo=false
  use_glib=false
  use_pulseaudio=false
  use_udev=false

  disable_file_support=true
  enable_websockets=false
  disable_ftp_support=true
  use_kerberos=false
  enable_mdns=false
  enable_reporting=false
  include_transport_security_state_preload_list=false
  rtc_use_pipewire=false
'

if [ "$(uname)" = Linux ]; then
  flags="$flags"'
    use_xkbcommon=false
    use_system_libdrm=false
    use_ozone=true
    ozone_auto_platforms=false
    ozone_platform="headless"
    ozone_platform_headless=true'
  . ./get-sysroot.sh
  sysroot=$(get_sysroot)
  if [ "$sysroot" ]; then
      flags="$flags
        sysroot=\"//$sysroot\""
  fi
fi

case "$(uname)" in
  MINGW*|MSYS*)
    case "$(uname -m)" in
      x86_64) PGO_NAME=win64;;
      *) PGO_NAME=win32;;
    esac;;
  Darwin) PGO_NAME=mac;;
esac
if [ "$PGO_NAME" ]; then
  profile=$(cat chrome/build/$PGO_NAME.pgo.txt)
  flags="$flags
    pgo_data_path=\"../../chrome/build/pgo_profiles/$profile\""
fi

rm -rf "./$out"
mkdir -p out

python=$(which python2 2>/dev/null || which python 2>/dev/null || which python3 2>/dev/null)
export DEPOT_TOOLS_WIN_TOOLCHAIN=0

./gn/out/gn gen "$out" --args="$flags $EXTRA_FLAGS" --script-executable=$python

ninja -C "$out" naive
