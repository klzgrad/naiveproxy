# Copyright (c) 2022, Google Inc.
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

# This script runs test_fips repeatedly with different FIPS tests broken. It is
# intended to be observed to demonstrate that the various tests are working and
# thus pauses for a keystroke between tests.

set -e

TEST_FIPS_BIN="build/util/fipstools/test_fips"

if [ ! -f $TEST_FIPS_BIN ]; then
  echo "$TEST_FIPS_BIN is missing. Run this script from the top level of a"
  echo "BoringSSL checkout and ensure that BoringSSL has been built in"
  echo "build/ with -DFIPS_BREAK_TEST=TESTS passed to CMake."
  exit 1
fi

KATS=$(go run util/fipstools/break-kat.go --list-tests)

echo -e '\033[1mNormal output\033[0m'
$TEST_FIPS_BIN
read

echo
echo -e '\033[1mIntegrity test failure\033[0m'
go run util/fipstools/break-hash.go $TEST_FIPS_BIN break-bin
chmod u+x ./break-bin
./break-bin || true
rm ./break-bin
read

for kat in $KATS; do
  echo
  echo -e "\033[1mKAT failure ${kat}\033[0m"
  go run util/fipstools/break-kat.go $TEST_FIPS_BIN $kat > break-bin
  chmod u+x ./break-bin
  ./break-bin || true
  rm ./break-bin
  read
done
