#!/bin/sh

[ "$1" ] || exit 1
naive="$1"
if [ "$(uname)" = Linux ]; then
  . src/get-sysroot.sh
  sysroot=$(get_sysroot)
  eval "$EXTRA_FLAGS"
  case "$target_cpu" in
    arm64) naive="qemu-aarch64 -L src/$sysroot $naive";;
    arm) naive="qemu-arm -L src/$sysroot $naive";;
    x64) naive="qemu-x86_64 -L src/$sysroot $naive";;
    x86) naive="qemu-i386 -L src/$sysroot $naive";;
    mipsel) naive="qemu-mipsel -L src/$sysroot $naive";;
  esac
fi

set -ex

MSYS_NO_PATHCONV=1 openssl req -new -x509 -keyout server.pem -out server.pem -days 1 -nodes -subj '/C=XX'
cat >server.py <<EOF
import http.server, ssl
httpd = http.server.HTTPServer(('127.0.0.1', 60443), http.server.SimpleHTTPRequestHandler)
httpd.socket = ssl.wrap_socket(httpd.socket, server_side=True, certfile='server.pem')
httpd.serve_forever()
EOF
echo Hello >hello.txt
python3=$(which python3 2>/dev/null || which python 2>/dev/null)
$python3 server.py &
trap "rm -f server.py server.pem hello.txt; kill $!" EXIT

alias curl='curl -v --retry-connrefused --retry-delay 1 --retry 5'
curl -k https://127.0.0.1:60443/hello.txt

test_proxy() {
  curl --proxy "$1" -k https://127.0.0.1:60443/hello.txt | grep 'Hello'
}

test_naive() {
  test_name="$1"
  proxy="$2"
  echo "TEST '$test_name':"
  shift 2
  if (
    trap 'kill $pid' EXIT
    pid=
    for arg in "$@"; do
      $naive $arg & pid="$pid $!"
      sleep 3
    done
    test_proxy "$proxy"
  ); then
    echo "TEST '$test_name': PASS"
    true
  else
    echo "TEST '$test_name': FAIL"
    false
  fi
}

test_naive 'Default config' socks5h://127.0.0.1:1080 '--log'

echo '{"listen":"socks://127.0.0.1:60101","log":""}' >config.json
test_naive 'Default config file' socks5h://127.0.0.1:60101 ''
rm -f config.json

echo '{"listen":"socks://127.0.0.1:60201","log":""}' >/tmp/config.json
test_naive 'Config file' socks5h://127.0.0.1:60201 '/tmp/config.json'
rm -f /tmp/config.json

test_naive 'Trivial - listen scheme only' socks5h://127.0.0.1:1080 \
  '--log --listen=socks://'

test_naive 'Trivial - listen no host' socks5h://127.0.0.1:60301 \
  '--log --listen=socks://:60301'

test_naive 'Trivial - listen no port' socks5h://127.0.0.1:1080 \
  '--log --listen=socks://127.0.0.1'

test_naive 'Trivial - auth' socks5h://user:pass@127.0.0.1:60311 \
  '--log --listen=socks://user:pass@127.0.0.1:60311'

test_naive 'Trivial - auth with special chars' socks5h://user:^@127.0.0.1:60312 \
  '--log --listen=socks://user:^@127.0.0.1:60312'

test_naive 'Trivial - auth with special chars' socks5h://^:^@127.0.0.1:60313 \
  '--log --listen=socks://^:^@127.0.0.1:60313'

test_naive 'Trivial - auth with empty pass' socks5h://user:@127.0.0.1:60314 \
  '--log --listen=socks://user:@127.0.0.1:60314'

test_naive 'SOCKS-SOCKS' socks5h://127.0.0.1:60401 \
  '--log --listen=socks://:60401 --proxy=socks://127.0.0.1:60402' \
  '--log --listen=socks://:60402'

test_naive 'SOCKS-SOCKS - proxy no port' socks5h://127.0.0.1:60501 \
  '--log --listen=socks://:60501 --proxy=socks://127.0.0.1' \
  '--log --listen=socks://:1080'

test_naive 'SOCKS-HTTP' socks5h://127.0.0.1:60601 \
  '--log --listen=socks://:60601 --proxy=http://127.0.0.1:60602' \
  '--log --listen=http://:60602'

test_naive 'HTTP-HTTP' http://127.0.0.1:60701 \
  '--log --listen=http://:60701 --proxy=http://127.0.0.1:60702' \
  '--log --listen=http://:60702'

test_naive 'HTTP-SOCKS' http://127.0.0.1:60801 \
  '--log --listen=http://:60801 --proxy=socks://127.0.0.1:60802' \
  '--log --listen=socks://:60802'

test_naive 'SOCKS-HTTP padded' socks5h://127.0.0.1:60901 \
  '--log --listen=socks://:60901 --proxy=http://127.0.0.1:60902 --padding' \
  '--log --listen=http://:60902 --padding'

test_naive 'SOCKS-SOCKS-SOCKS' socks5h://127.0.0.1:61001 \
  '--log --listen=socks://:61001 --proxy=socks://127.0.0.1:61002' \
  '--log --listen=socks://:61002 --proxy=socks://127.0.0.1:61003' \
  '--log --listen=socks://:61003'

test_naive 'SOCKS-HTTP-SOCKS' socks5h://127.0.0.1:61101 \
  '--log --listen=socks://:61101 --proxy=http://127.0.0.1:61102' \
  '--log --listen=http://:61102 --proxy=socks://127.0.0.1:61103' \
  '--log --listen=socks://:61103'

test_naive 'HTTP-SOCKS-HTTP' http://127.0.0.1:61201 \
  '--log --listen=http://:61201 --proxy=socks://127.0.0.1:61202' \
  '--log --listen=socks://:61202 --proxy=http://127.0.0.1:61203' \
  '--log --listen=http://:61203'

test_naive 'HTTP-HTTP-HTTP' http://127.0.0.1:61301 \
  '--log --listen=http://:61301 --proxy=http://127.0.0.1:61302' \
  '--log --listen=http://:61302 --proxy=http://127.0.0.1:61303' \
  '--log --listen=http://:61303'
