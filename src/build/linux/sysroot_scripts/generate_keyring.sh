#!/bin/bash
# Copyright 2019 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -o nounset
set -o errexit

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

KEYS=(
    # Debian Ports Archive Automatic Signing Key (2023)
    "B523E5F3FC4E5F2C"
    # Debian Ports Archive Automatic Signing Key (2024)
    "8D69674688B6CB36"
    # Debian Archive Automatic Signing Key (12/bookworm)
    "B7C5D7D6350947F8"
    # Debian Security Archive Automatic Signing Key (12/bookworm)
    "254CF3B5AEC0A8F0"
    # Debian Stable Release Key (12/bookworm)
    "F8D2585B8783D481"
    # Debian Archive Automatic Signing Key (11/bullseye)
    "73A4F27B8DD47936"
    # Debian Security Archive Automatic Signing Key (11/bullseye)
    "A48449044AAD5C5D"
    # Debian Stable Release Key (11/bullseye)
    "605C66F00D6C9793"
    # Debian Stable Release Key (10/buster)
    "DCC9EFBF77E11517"
    # Debian Archive Automatic Signing Key (10/buster)
    "DC30D7C23CBBABEE"
    # Debian Security Archive Automatic Signing Key (10/buster)
    "4DFAB270CAA96DFA"
)

gpg --keyserver keyserver.ubuntu.com --recv-keys ${KEYS[@]}
gpg --output "${SCRIPT_DIR}/keyring.gpg" --export ${KEYS[@]}
