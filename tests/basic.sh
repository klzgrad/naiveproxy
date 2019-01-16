#!/bin/sh

[ "$1" ] || exit 1
naive="$1"
set -ex

test_proxy() {
  curl -v --proxy "$1" https://www.google.com/humans.txt | grep '^Google is built'
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
    done
    sleep 1
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

echo '{"listen":"socks://127.0.0.1:61080","log":""}' >config.json
test_naive 'Default config file' socks5h://127.0.0.1:61080 ''
rm -f config.json

echo '{"listen":"socks://127.0.0.1:61080","log":""}' >/tmp/config.json
test_naive 'Config file' socks5h://127.0.0.1:61080 '/tmp/config.json'
rm -f /tmp/config.json

test_naive 'Trivial - listen scheme only' socks5h://127.0.0.1:1080 \
  '--log --listen=socks://'

test_naive 'Trivial - listen no host' socks5h://127.0.0.1:61080 \
  '--log --listen=socks://:61080'

test_naive 'Trivial - listen no port' socks5h://127.0.0.1:1080 \
  '--log --listen=socks://127.0.0.1'

test_naive 'SOCKS-SOCKS' socks5h://127.0.0.1:11080 \
  '--log --listen=socks://:11080 --proxy=socks://127.0.0.1:21080' \
  '--log --listen=socks://:21080'

test_naive 'SOCKS-SOCKS - proxy no port' socks5h://127.0.0.1:11080 \
  '--log --listen=socks://:11080 --proxy=socks://127.0.0.1' \
  '--log --listen=socks://:1080'

test_naive 'SOCKS-HTTP' socks5h://127.0.0.1:11080 \
  '--log --listen=socks://:11080 --proxy=http://127.0.0.1:28080' \
  '--log --listen=http://:28080'

test_naive 'HTTP-HTTP' http://127.0.0.1:18080 \
  '--log --listen=http://:18080 --proxy=http://127.0.0.1:28080' \
  '--log --listen=http://:28080'

test_naive 'HTTP-SOCKS' http://127.0.0.1:18080 \
  '--log --listen=http://:18080 --proxy=http://127.0.0.1:21080' \
  '--log --listen=http://:21080'

test_naive 'SOCKS-HTTP padded' socks5h://127.0.0.1:11080 \
  '--log --listen=socks://:11080 --proxy=http://127.0.01:28080 --padding' \
  '--log --listen=http://:28080 --padding'

test_naive 'SOCKS-SOCKS-SOCKS' socks5h://127.0.0.1:11080 \
  '--log --listen=socks://:11080 --proxy=socks://127.0.0.1:21080' \
  '--log --listen=socks://:21080 --proxy=socks://127.0.0.1:31080' \
  '--log --listen=socks://:31080'

test_naive 'SOCKS-HTTP-SOCKS' socks5h://127.0.0.1:11080 \
  '--log --listen=socks://:11080 --proxy=socks://127.0.0.1:28080' \
  '--log --listen=socks://:28080 --proxy=socks://127.0.0.1:31080' \
  '--log --listen=socks://:31080'

test_naive 'HTTP-SOCKS-HTTP' socks5h://127.0.0.1:18080 \
  '--log --listen=socks://:18080 --proxy=socks://127.0.0.1:21080' \
  '--log --listen=socks://:21080 --proxy=socks://127.0.0.1:38080' \
  '--log --listen=socks://:38080'

test_naive 'HTTP-HTTP-HTTP' socks5h://127.0.0.1:18080 \
  '--log --listen=socks://:18080 --proxy=socks://127.0.0.1:28080' \
  '--log --listen=socks://:28080 --proxy=socks://127.0.0.1:38080' \
  '--log --listen=socks://:38080'
