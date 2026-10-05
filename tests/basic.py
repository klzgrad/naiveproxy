#!/usr/bin/env python3
"""Exercise naive's configuration, authentication, and proxy chaining.

Run with --naive=/path/to/naive and --server_protocol=https or http.
Four cases require port 1080; use --skip-default-port-tests if it is occupied.
The existing --rootfs and --target_cpu options support bwrap and qemu-user.
"""

import argparse
from collections import deque
import contextlib
import http.server
import os
from pathlib import Path
import queue
import shlex
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


HOST = '127.0.0.1'
PAYLOAD = b'Naive basic integration test\n' * 128
QEMU_CPUS = {
    'arm64': 'aarch64',
    'arm': 'arm',
    'mipsel': 'mipsel',
    'mips64el': 'mips64el',
    'riscv64': 'riscv64',
    'loong64': 'loong64',
}


class TestFailure(Exception):
    pass


class ListenFailure(TestFailure):
    pass


class OriginHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Length', str(len(PAYLOAD)))
        self.end_headers()
        self.wfile.write(PAYLOAD)

    def log_message(self, *args):
        pass


@contextlib.contextmanager
def origin_server(protocol, directory):
    with http.server.ThreadingHTTPServer((HOST, 0), OriginHandler) as server:
        if protocol == 'https':
            certfile = directory / 'origin.pem'
            # A P-256 key is quick to generate. This certificate belongs to
            # curl's destination; naive only relays the encrypted bytes.
            result = subprocess.run(
                ['openssl', 'req', '-new', '-x509', '-newkey', 'ec',
                 '-pkeyopt', 'ec_paramgen_curve:prime256v1', '-nodes',
                 '-keyout', str(certfile), '-out', str(certfile),
                 '-days', '1', '-subj', '/CN=localhost'],
                capture_output=True, timeout=10, text=True)
            if result.returncode:
                raise TestFailure(f'Certificate generation failed:\n{result.stderr}')
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(certfile)
            server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(
            target=server.serve_forever, kwargs={'poll_interval': 0.05}, daemon=True)
        thread.start()
        try:
            yield server.server_port
        finally:
            server.shutdown()
            thread.join()


class BasicTests:
    def __init__(self, options, directory, origin_port):
        self.options = options
        self.directory = directory
        self.origin_port = origin_port
        self.passed = 0
        self.skipped = 0

    def request(self, proxy=None):
        url = f'{self.options.server_protocol}://{HOST}:{self.origin_port}/'
        # Ignore ~/.curlrc, NO_PROXY, and inherited proxy settings. Every
        # proxied case must actually traverse the client under test.
        command = ['curl', '--disable', '--insecure', '--silent', '--show-error',
                   '--fail', '--max-time', '10', '--noproxy', '',
                   '--proxy', proxy or '', url]
        result = subprocess.run(command, capture_output=True, timeout=15)
        if result.returncode or result.stdout != PAYLOAD:
            raise TestFailure(
                f'{shlex.join(command)} failed (exit {result.returncode}):\n'
                f'{result.stderr.decode(errors="replace")}\n'
                f'Received {len(result.stdout)} bytes; expected {len(PAYLOAD)}')

    @contextlib.contextmanager
    def start_naive(self, arguments):
        qemu = QEMU_CPUS.get(self.options.target_cpu)
        if self.options.rootfs:
            if qemu:
                command = [f'qemu-{qemu}', '-L', self.options.rootfs,
                           self.options.naive]
            else:
                # Mount the private test directory and binary rather than
                # copying config files or replacing /naive in the rootfs.
                (self.directory / 'naive').touch(exist_ok=True)
                command = ['bwrap', '--die-with-parent',
                           '--bind', self.options.rootfs, '/',
                           '--bind', str(self.directory), '/tmp',
                           '--ro-bind', self.options.naive, '/tmp/naive',
                           '--proc', '/proc', '--dev', '/dev',
                           '--chdir', '/tmp', '/tmp/naive']
        else:
            command = [self.options.naive]
        command.extend(arguments)
        env = os.environ.copy()
        env['TEST_MARK_STARTUP'] = 'yes'
        proc = subprocess.Popen(command, cwd=self.directory, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                text=True, encoding='utf-8', errors='replace')
        lines = deque(maxlen=2000)
        startup = queue.Queue()

        def drain_stderr():
            try:
                for line in proc.stderr:
                    lines.append(line)
                    if 'Failed to listen' in line or 'TEST_MARK_STARTUP' in line:
                        startup.put(line)
            finally:
                startup.put(None)

        reader = threading.Thread(target=drain_stderr, daemon=True)
        reader.start()
        failed = False
        try:
            # Keep the existing allowance for slow qemu-user startup.
            deadline = time.monotonic() + 20
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TestFailure('naive did not start within 20 seconds')
                try:
                    line = startup.get(timeout=remaining)
                except queue.Empty:
                    raise TestFailure('naive did not start within 20 seconds') from None
                if line is None:
                    raise TestFailure(f'naive exited before startup (exit {proc.wait()})')
                if 'Failed to listen' in line:
                    raise ListenFailure(line.strip())
                if 'TEST_MARK_STARTUP' in line:
                    break
            yield proc
        except BaseException:
            failed = True
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            reader.join()
            proc.stderr.close()
            if failed:
                print(f'Client log: {shlex.join(command)}', file=sys.stderr)
                print(''.join(lines), file=sys.stderr, end='')

    def test_naive_once(self, proxy, *arguments, **kwargs):
        allocated = set()

        class PortDict(dict):
            def __missing__(self, key):
                if not key.startswith('PORT'):
                    return key
                # Binding without port reuse detects occupied listeners,
                # including naive listeners that enable SO_REUSEPORT.
                while True:
                    with socket.socket() as sock:
                        sock.bind((HOST, 0))
                        port = sock.getsockname()[1]
                    if port >= 1024 and port not in allocated:
                        break
                allocated.add(port)
                self[key] = str(port)
                return self[key]

        ports = PortDict()
        proxy = proxy.format_map(ports)
        config_file = kwargs.get('config_file')
        config_content = kwargs.get('config_content')
        config_path = self.directory / config_file if config_file else None
        try:
            if config_content is not None:
                config_path.write_text('{' + config_content.format_map(ports) + '}')
            with contextlib.ExitStack() as clients:
                processes = []
                for arguments_instance in arguments:
                    naive_arguments = shlex.split(arguments_instance.format_map(ports))
                    processes.append(clients.enter_context(self.start_naive(naive_arguments)))
                self.request(proxy)
                if any(proc.poll() is not None for proc in processes):
                    raise TestFailure('A client exited during the request')
        finally:
            if config_path is not None:
                config_path.unlink(missing_ok=True)

    def test_naive(self, label, proxy, *arguments, **kwargs):
        reason = None
        if self.options.target_cpu == 'arm' and not label.startswith('Default'):
            # Preserve the existing skips for slow arm qemu-user tests.
            reason = 'slow arm qemu-user test'
        elif kwargs.get('uses_default_port'):
            if self.options.skip_default_port_tests:
                reason = '--skip-default-port-tests'
            else:
                with socket.socket() as sock:
                    # Allow old connections in TIME_WAIT, but do not enable
                    # SO_REUSEPORT, which would hide an active listener.
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    try:
                        sock.bind(('0.0.0.0', 1080))
                    except OSError:
                        raise TestFailure(
                            f'{label}: port 1080 is occupied; use '
                            '--skip-default-port-tests to omit the four cases '
                            'that require the default port') from None
        if reason:
            self.skipped += 1
            print(f'** SKIP TEST: {label} ({reason})', flush=True)
            return

        for attempt in range(5):
            try:
                self.test_naive_once(proxy, *arguments, **kwargs)
                break
            except ListenFailure:
                if attempt == 4:
                    raise
                # Cleanup has reaped every client. Retry with fresh ports;
                # there is no reason to sleep for a fixed second.
                print(f'Retrying {label} with new ports...', flush=True)
            except TestFailure as error:
                raise TestFailure(f'{label}: {error}') from error
        self.passed += 1
        print(f'** TEST PASS: {label}', flush=True)


def run_tests(test_naive):
    test_naive('Default config', 'socks5h://127.0.0.1:1080',
               '--log', uses_default_port=True)

    test_naive('Default config file', 'socks5h://127.0.0.1:{PORT1}',
               '',
               config_content='"listen":"socks://127.0.0.1:{PORT1}","log":""',
               config_file='config.json')

    test_naive('Custom config file', 'socks5h://127.0.0.1:{PORT1}',
               'custom.json',
               config_content='"listen":"socks://127.0.0.1:{PORT1}","log":""',
               config_file='custom.json')

    test_naive('Multiple listens - command line', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --listen=http://:{PORT2}')

    test_naive('Multiple listens - command line', 'http://127.0.0.1:{PORT2}',
               '--log --listen=socks://:{PORT1} --listen=http://:{PORT2}')

    test_naive('Multiple listens - config file', 'socks5h://127.0.0.1:{PORT1}',
               'multiple-listen.json',
               config_content='"listen":["socks://:{PORT1}", "http://:{PORT2}"],"log":""',
               config_file='multiple-listen.json')

    test_naive('Multiple listens - config file', 'http://127.0.0.1:{PORT2}',
               'multiple-listen.json',
               config_content='"listen":["socks://:{PORT1}", "http://:{PORT2}"],"log":""',
               config_file='multiple-listen.json')

    test_naive('Multiple proxies - command line', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --listen=socks://:{PORT2} --proxy=http://127.0.0.1:{PORT3} --proxy=http://127.0.0.1:{PORT4}',
               '--log --listen=http://:{PORT3} --listen=http://:{PORT4} --proxy=socks://127.0.0.1:{PORT5}',
               '--log --listen=socks://:{PORT5}')

    test_naive('Multiple proxies - command line', 'socks5h://127.0.0.1:{PORT2}',
               '--log --listen=socks://:{PORT1} --listen=socks://:{PORT2} --proxy=http://127.0.0.1:{PORT3} --proxy=http://127.0.0.1:{PORT4}',
               '--log --listen=http://:{PORT3} --listen=http://:{PORT4} --proxy=socks://127.0.0.1:{PORT5}',
               '--log --listen=socks://:{PORT5}')

    test_naive('Multiple proxies - different auth', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --listen=socks://:{PORT2} --proxy=http://user1:pass1@127.0.0.1:{PORT3} --proxy=http://user2:pass2@127.0.0.1:{PORT3}',
               '--log --listen=http://user1:pass1@127.0.0.1:{PORT3}')

    test_naive('Multiple proxies - different auth', 'socks5h://127.0.0.1:{PORT2}',
               '--log --listen=socks://:{PORT1} --listen=socks://:{PORT2} --proxy=http://user1:pass1@127.0.0.1:{PORT3} --proxy=http://user2:pass2@127.0.0.1:{PORT3}',
               '--log --listen=http://user2:pass2@127.0.0.1:{PORT3}')

    test_naive('Trivial - listen scheme only', 'socks5h://127.0.0.1:1080',
               '--log --listen=socks://', uses_default_port=True)

    test_naive('Trivial - listen no host', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1}')

    test_naive('Trivial - listen no port', 'socks5h://127.0.0.1:1080',
               '--log --listen=socks://127.0.0.1', uses_default_port=True)

    test_naive('Trivial - auth', 'socks5h://user:pass@127.0.0.1:{PORT1}',
               '--log --listen=socks://user:pass@127.0.0.1:{PORT1}')

    test_naive('Trivial - auth with special chars', 'socks5h://user:^@127.0.0.1:{PORT1}',
               '--log --listen=socks://user:^@127.0.0.1:{PORT1}')

    test_naive('Trivial - auth with special chars', 'socks5h://^:^@127.0.0.1:{PORT1}',
               '--log --listen=socks://^:^@127.0.0.1:{PORT1}')

    test_naive('Trivial - auth with empty pass', 'socks5h://user:@127.0.0.1:{PORT1}',
               '--log --listen=socks://user:@127.0.0.1:{PORT1}')

    test_naive('SOCKS-SOCKS', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --proxy=socks://127.0.0.1:{PORT2}',
               '--log --listen=socks://:{PORT2}')

    test_naive('SOCKS-SOCKS - proxy no port', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --proxy=socks://127.0.0.1',
               '--log --listen=socks://:1080', uses_default_port=True)

    test_naive('SOCKS-HTTP', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --proxy=http://127.0.0.1:{PORT2}',
               '--log --listen=http://:{PORT2}')

    test_naive('HTTP-HTTP', 'http://127.0.0.1:{PORT1}',
               '--log --listen=http://:{PORT1} --proxy=http://127.0.0.1:{PORT2}',
               '--log --listen=http://:{PORT2}')

    test_naive('HTTP-SOCKS', 'http://127.0.0.1:{PORT1}',
               '--log --listen=http://:{PORT1} --proxy=socks://127.0.0.1:{PORT2}',
               '--log --listen=socks://:{PORT2}')

    test_naive('SOCKS-SOCKS-SOCKS', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --proxy=socks://127.0.0.1:{PORT2}',
               '--log --listen=socks://:{PORT2} --proxy=socks://127.0.0.1:{PORT3}',
               '--log --listen=socks://:{PORT3}')

    test_naive('SOCKS-HTTP-SOCKS', 'socks5h://127.0.0.1:{PORT1}',
               '--log --listen=socks://:{PORT1} --proxy=http://127.0.0.1:{PORT2}',
               '--log --listen=http://:{PORT2} --proxy=socks://127.0.0.1:{PORT3}',
               '--log --listen=socks://:{PORT3}')

    test_naive('HTTP-SOCKS-HTTP', 'http://127.0.0.1:{PORT1}',
               '--log --listen=http://:{PORT1} --proxy=socks://127.0.0.1:{PORT2}',
               '--log --listen=socks://:{PORT2} --proxy=http://127.0.0.1:{PORT3}',
               '--log --listen=http://:{PORT3}')

    test_naive('HTTP-HTTP-HTTP', 'http://127.0.0.1:{PORT1}',
               '--log --listen=http://:{PORT1} --proxy=http://127.0.0.1:{PORT2}',
               '--log --listen=http://:{PORT2} --proxy=http://127.0.0.1:{PORT3}',
               '--log --listen=http://:{PORT3}')

    test_naive('HTTP-HTTP (with auth)', 'http://127.0.0.1:{PORT1}',
               '--log --listen=http://:{PORT1} --proxy=http://hello:world@127.0.0.1:{PORT2}',
               '--log --listen=http://hello:world@127.0.0.1:{PORT2}')

    test_naive('HTTPa-HTTPb,HTTPc (chaining with remote loop)', 'http://127.0.0.1:{PORT1}',
               '--log --listen=http://:{PORT2}',
               '--log --listen=http://:{PORT1} --proxy=http://127.0.0.1:{PORT2},http://127.0.0.1:{PORT2}')

    test_naive('HTTPa-HTTPb,HTTPc (chaining with multiple auth)', 'http://127.0.0.1:{PORT1}',
               '--log --listen=http://hello:world2@127.0.0.1:{PORT2}',
               '--log --listen=http://hello:world3@127.0.0.1:{PORT3}',
               '--log --listen=http://127.0.0.1:{PORT1} --proxy=http://hello:world2@127.0.0.1:{PORT2},http://hello:world3@127.0.0.1:{PORT3}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--naive', required=True)
    parser.add_argument('--rootfs')
    parser.add_argument('--target_cpu')
    parser.add_argument('--server_protocol', choices=['http', 'https'], default='https')
    parser.add_argument('--skip-default-port-tests', action='store_true',
                        help='skip the four cases that require port 1080')
    options = parser.parse_args()
    options.naive = str(Path(options.naive).resolve())
    if options.rootfs:
        options.rootfs = str(Path(options.rootfs).resolve())
    started = time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix='naive-basic-') as tmp:
            directory = Path(tmp)
            with origin_server(options.server_protocol, directory) as origin_port:
                tests = BasicTests(options, directory, origin_port)
                tests.request()
                run_tests(tests.test_naive)
        print(f'{options.server_protocol}: {tests.passed} passed, '
              f'{tests.skipped} skipped in {time.monotonic() - started:.2f}s')
        return 0
    except (TestFailure, OSError, subprocess.TimeoutExpired) as error:
        print(f'** TEST FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
