#!/usr/bin/env python3
"""Test SSL_CERT_FILE and SSL_CERT_DIR with HTTPS and QUIC (no root required).

Usage: python3 tests/ssl_cert_file.py --naive=src/out/Release/naive
Requires openssl, curl, and Caddy with the naive forwardproxy plugin. Caddy
defaults to tests/caddy; use --caddy to select another binary. All traffic is
local, and certificates, configs, and logs live in a temporary directory.
"""

import argparse
import contextlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time


TESTS_DIR = Path(__file__).resolve().parent
CERTS_DIR = TESTS_DIR.parent / 'src/net/tools/quic/certs'
PAYLOAD = b'Local CA integration test\n' * 1024


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, timeout=30, **kwargs)
    if result.returncode:
        raise RuntimeError(
            f'{command} exited with {result.returncode}:\n'
            f'{result.stderr.decode(errors="replace")}')
    return result


def generate_certs(directory):
    # The Chromium script uses relative paths and removes ./out. Copy it and
    # its configs so it runs exclusively inside our temporary directory.
    directory.mkdir()
    for name in ('generate-certs.sh', 'ca.cnf', 'leaf.cnf'):
        shutil.copyfile(CERTS_DIR / name, directory / name)
    env = os.environ.copy()
    # These variables override the defaults in Chromium's OpenSSL configs.
    for name in ('CA_DIR', 'KEY_SIZE', 'ALGO', 'CERT_TYPE', 'CA_NAME',
                 'SUBJECT_NAME'):
        env.pop(name, None)
    run(['sh', 'generate-certs.sh'], cwd=directory, env=env)
    return directory / 'out'


def unused_port(udp=False):
    # QUIC needs a port available for both TCP and UDP. Hold both sockets
    # while checking; the short gap before Caddy binds is unavoidable.
    for _ in range(100):
        with socket.socket() as tcp:
            tcp.bind(('127.0.0.1', 0))
            port = tcp.getsockname()[1]
            if port < 1024:
                continue
            if udp:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    try:
                        sock.bind(('127.0.0.1', port))
                    except OSError:
                        continue
            return port
    raise RuntimeError('Could not allocate an unprivileged loopback port')


def netlog_events(netlog):
    # Naive exits on SIGTERM without closing the NetLog JSON document. Each
    # complete event is on its own line, so read those instead of the wrapper.
    for line in netlog.read_text().splitlines():
        try:
            event = json.loads(line.rstrip(','))
        except json.JSONDecodeError:
            continue
        if isinstance(event, dict) and 'type' in event:
            yield event


@contextlib.contextmanager
def running(command, directory, env, label, startup_marker):
    log = directory / f'{label}.log'
    with log.open('w') as output:
        proc = subprocess.Popen(command, cwd=directory, env=env,
                                stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(f'{label} exited with {proc.returncode}')
                if startup_marker in log.read_text(errors='replace'):
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError(f'{label} did not start within 10 seconds')
                time.sleep(0.05)
            yield proc
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()


class OriginHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Length', str(len(PAYLOAD)))
        self.end_headers()
        self.wfile.write(PAYLOAD)

    def log_message(self, *args):
        pass


def test_protocol(protocol, naive, caddy, directory, certs, origin_port):
    directory.mkdir()
    root_cert = certs / '2048-sha256-root.pem'
    empty_ca_file = directory / 'empty-ca.pem'
    empty_ca_file.write_text('')
    empty_ca_dir = directory / 'empty-ca-dir'
    empty_ca_dir.mkdir()
    trusted_ca_dir = directory / 'trusted-ca-dir'
    trusted_ca_dir.mkdir()
    # Only the CA belongs in this directory, not the leaf or any private keys.
    shutil.copyfile(root_cert, trusted_ca_dir / 'local-ca.pem')
    cases = (
        ('untrusted', empty_ca_file, empty_ca_dir, False),
        ('SSL_CERT_FILE', root_cert, empty_ca_dir, True),
        ('SSL_CERT_DIR', empty_ca_file, trusted_ca_dir, True),
        ('SSL_CERT_DIR-list', empty_ca_file,
         f'{empty_ca_dir}{os.pathsep}{trusted_ca_dir}', True),
    )
    caddy_port = unused_port(udp=protocol == 'quic')
    config = directory / 'Caddyfile'
    # Restrict the server to the requested transport so QUIC cannot pass by
    # falling back to HTTPS. No admin listener, port 80 redirect, ACME, or
    # system trust installation is needed. Only the local origin is allowed.
    protocols = 'h1 h2' if protocol == 'https' else 'h3'
    config.write_text(f'''{{
    admin off
    auto_https off
    persist_config off
    servers {{
        protocols {protocols}
    }}
}}
https://127.0.0.1:{caddy_port} {{
    bind 127.0.0.1
    tls "{certs / 'leaf_cert.pem'}" "{certs / 'leaf_cert.key'}"
    route {{
        forward_proxy {{
            basic_auth user pass
            ports {origin_port}
            acl {{
                allow 127.0.0.1
                deny all
            }}
        }}
        respond "Caddy local CA test"
    }}
}}
''')
    caddy_env = os.environ.copy()
    caddy_env['XDG_DATA_HOME'] = str(directory / 'data')
    caddy_env['XDG_CONFIG_HOME'] = str(directory / 'config')
    with running([caddy, 'run', '--config', str(config), '--adapter', 'caddyfile'],
                 directory, caddy_env, 'caddy', 'serving initial configuration'):
        for label, ca_file, ca_dirs, trusted in cases:
            listen_port = unused_port()
            netlog = directory / f'{label}-netlog.json'
            env = os.environ.copy()
            # Override both variables, including system-default fallbacks, so
            # success can only come from the trust source this case supplies.
            env['SSL_CERT_FILE'] = str(ca_file)
            env['SSL_CERT_DIR'] = str(ca_dirs)
            env['TEST_MARK_STARTUP'] = 'yes'
            command = [naive, f'--listen=socks://127.0.0.1:{listen_port}',
                       f'--proxy={protocol}://user:pass@127.0.0.1:{caddy_port}',
                       '--log', f'--log-net-log={netlog}']
            with running(command, directory, env, label, 'TEST_MARK_STARTUP') as proc:
                # --disable ignores ~/.curlrc; --noproxy overrides NO_PROXY.
                # The origin uses HTTP: TLS verification under test belongs
                # to naive's connection to Caddy, not to curl.
                result = subprocess.run(
                    ['curl', '--disable', '--silent', '--show-error', '--fail',
                     '--max-time', '10', '--noproxy', '', '--proxy',
                     f'socks5h://127.0.0.1:{listen_port}',
                     f'http://127.0.0.1:{origin_port}/'],
                    capture_output=True, timeout=15)
                if proc.poll() is not None:
                    raise RuntimeError(f'{protocol}/{label}: naive exited')
            if trusted:
                if result.returncode or result.stdout != PAYLOAD:
                    raise RuntimeError(
                        f'{protocol}/{label}: failed with trusted CA '
                        f'(curl exit {result.returncode}):\n'
                        f'{result.stderr.decode(errors="replace")}')
            else:
                if result.returncode == 0:
                    raise RuntimeError(f'{protocol}: accepted an untrusted CA')
                # A timeout or connection failure alone cannot prove that
                # certificate verification ran. Require its authority error.
                if not any(event.get('params', {}).get('net_error') == -202
                           for event in netlog_events(netlog)):
                    raise RuntimeError(
                        f'{protocol}: expected ERR_CERT_AUTHORITY_INVALID '
                        f'with empty CA sources, curl exit {result.returncode}')
            print(f'PASS: {protocol} {label}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--naive', required=True, type=Path)
    parser.add_argument('--caddy', default=TESTS_DIR / 'caddy', type=Path)
    args = parser.parse_args()
    naive, caddy = str(args.naive.resolve()), str(args.caddy.resolve())
    for binary in (naive, caddy):
        if not os.access(binary, os.X_OK):
            parser.error(f'Not an executable: {binary}')
    for tool in ('openssl', 'curl'):
        if shutil.which(tool) is None:
            parser.error(f'Required executable not found: {tool}')

    with tempfile.TemporaryDirectory(prefix='naive-ssl-cert-file-') as tmp:
        directory = Path(tmp)
        try:
            certs = generate_certs(directory / 'certs')
            with http.server.ThreadingHTTPServer(
                    ('127.0.0.1', 0), OriginHandler) as origin:
                thread = threading.Thread(target=origin.serve_forever, daemon=True)
                thread.start()
                try:
                    for protocol in ('https', 'quic'):
                        test_protocol(protocol, naive, caddy, directory / protocol,
                                      certs, origin.server_port)
                finally:
                    origin.shutdown()
                    thread.join()
        except Exception as error:
            print(f'FAIL: {error}', file=sys.stderr)
            for log in sorted(directory.glob('*/*.log')):
                print(f'\n--- {log.parent.name}/{log.name} ---', file=sys.stderr)
                print(log.read_text(errors='replace'), file=sys.stderr)
            for netlog in sorted(directory.glob('*/*-netlog.json')):
                print(f'\n--- {netlog.parent.name}/{netlog.name} verification/errors ---',
                      file=sys.stderr)
                for event in netlog_events(netlog):
                    params = event.get('params', {})
                    if ('cert_status' in params or
                            any('error' in key and value for key, value in params.items())):
                        print({key: value for key, value in params.items()
                               if 'error' in key or key in (
                                   'details', 'cert_status', 'is_issued_by_known_root')},
                              file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
