# Copyright (c) 2015, Google Inc.
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

vars = {
  'chromium_git': 'https://chromium.googlesource.com',

  'checkout_clang': False,
  'checkout_fuzzer': False,
  'checkout_sde': False,
}

deps = {
  'boringssl/util/bot/android_ndk': {
    'url': Var('chromium_git') + '/android_ndk.git' + '@' + '635bc380968a76f6948fee65f80a0b28db53ae81',
    'condition': 'checkout_android',
  },

'boringssl/util/bot/android_tools': {
    'url': Var('chromium_git') + '/android_tools.git' + '@' + 'c22a664c39af72dd8f89200220713dcad811300a',
    'condition': 'checkout_android',
  },

  'boringssl/util/bot/gyp':
    Var('chromium_git') + '/external/gyp.git' + '@' + 'd61a9397e668fa9843c4aa7da9e79460fe590bfb',

  'boringssl/util/bot/libFuzzer': {
    'url': Var('chromium_git') + '/chromium/llvm-project/compiler-rt/lib/fuzzer.git' + '@' + 'ba2c1cd6f87accb32b5dbce297387c56a2e53a2f',
    'condition': 'checkout_fuzzer',
  },
}

recursedeps = [
  # android_tools pulls in the NDK from a separate repository.
  'boringssl/util/bot/android_tools',
]

hooks = [
  {
    'name': 'cmake_linux64',
    'pattern': '.',
    'condition': 'host_os == "linux"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=linux*',
                '--no_auth',
                '--bucket', 'chromium-tools',
                '-s', 'boringssl/util/bot/cmake-linux64.tar.gz.sha1',
    ],
  },
  {
    'name': 'cmake_linux64_extract',
    'pattern': '.',
    'condition': 'host_os == "linux"',
    'action': [ 'python',
                'boringssl/util/bot/extract.py',
                'boringssl/util/bot/cmake-linux64.tar.gz',
                'boringssl/util/bot/cmake-linux64/',
    ],
  },
  {
    'name': 'cmake_mac',
    'pattern': '.',
    'condition': 'host_os == "mac"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=darwin',
                '--no_auth',
                '--bucket', 'chromium-tools',
                '-s', 'boringssl/util/bot/cmake-mac.tar.gz.sha1',
    ],
  },
  {
    'name': 'cmake_mac_extract',
    'pattern': '.',
    'condition': 'host_os == "mac"',
    'action': [ 'python',
                'boringssl/util/bot/extract.py',
                'boringssl/util/bot/cmake-mac.tar.gz',
                'boringssl/util/bot/cmake-mac/',
    ],
  },
  {
    'name': 'cmake_win32',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-tools',
                '-s', 'boringssl/util/bot/cmake-win32.zip.sha1',
    ],
  },
  {
    'name': 'cmake_win32_extract',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': [ 'python',
                'boringssl/util/bot/extract.py',
                'boringssl/util/bot/cmake-win32.zip',
                'boringssl/util/bot/cmake-win32/',
    ],
  },
  {
    'name': 'perl_win32',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-tools',
                '-s', 'boringssl/util/bot/perl-win32.zip.sha1',
    ],
  },
  {
    'name': 'perl_win32_extract',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': [ 'python',
                'boringssl/util/bot/extract.py',
                '--no-prefix',
                'boringssl/util/bot/perl-win32.zip',
                'boringssl/util/bot/perl-win32/',
    ],
  },
  {
    'name': 'yasm_win32',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-tools',
                '-s', 'boringssl/util/bot/yasm-win32.exe.sha1',
    ],
  },
  {
    'name': 'win_toolchain',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': [ 'python',
                'boringssl/util/bot/vs_toolchain.py',
                'update',
    ],
  },
  {
    'name': 'clang',
    'pattern': '.',
    'condition': 'checkout_clang',
    'action': [ 'python',
                'boringssl/util/bot/update_clang.py',
    ],
  },
  {
    'name': 'sde_linux64',
    'pattern': '.',
    'condition': 'checkout_sde and host_os == "linux"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--bucket', 'chrome-boringssl-sde',
                '-s', 'boringssl/util/bot/sde-linux64.tar.bz2.sha1'
    ],
  },
  {
    'name': 'sde_linux64_extract',
    'pattern': '.',
    'condition': 'checkout_sde and host_os == "linux"',
    'action': [ 'python',
                'boringssl/util/bot/extract.py',
                'boringssl/util/bot/sde-linux64.tar.bz2',
                'boringssl/util/bot/sde-linux64/',
    ],
  },
]
