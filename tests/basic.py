#!/usr/bin/env python3
import argparse
import http.server
import os
import shutil
import ssl
import subprocess
import tempfile
import threading
import time

parser = argparse.ArgumentParser()
parser.add_argument('--naive', required=True)
parser.add_argument('--rootfs')
parser.add_argument('--target_cpu')
parser.add_argument('--server_protocol', choices=['http', 'https'], default='https')
argv = parser.parse_args()

if argv.rootfs:
    try:
        os.remove(os.path.join(argv.rootfs, 'naive'))
    except OSError:
        pass

server_protocol = argv.server_protocol

_, certfile = tempfile.mkstemp()

result = subprocess.run(
    f'openssl req -new -x509 -keyout {certfile} -out {certfile} -days 1 -nodes -subj /C=XX'.split(), capture_output=True)
result.check_returncode()

HTTPS_SERVER_HOSTNAME = '127.0.0.1'
HTTP_SERVER_PORT = 60443 if server_protocol == 'https' else 60080

httpd = http.server.HTTPServer(
    (HTTPS_SERVER_HOSTNAME, HTTP_SERVER_PORT), http.server.SimpleHTTPRequestHandler)
httpd.timeout = 1
httpd.allow_reuse_address = True
if server_protocol == 'https':
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_context.load_cert_chain(certfile=certfile)
    httpd.socket = ssl_context.wrap_socket(httpd.socket, server_side=True)

httpd_thread = threading.Thread(
    target=lambda httpd: httpd.serve_forever(), args=(httpd,), daemon=True)
httpd_thread.start()


def test_https_server(hostname, port, proxy=None):
    url = f'{server_protocol}://{hostname}:{port}/404'
    cmdline = ['curl', '-k', '-s']
    if proxy:
        cmdline.extend(['--proxy', proxy])
    cmdline.append(url)
    print('subprocess.run', ' '.join(cmdline))
    result = subprocess.run(cmdline, capture_output=True,
                            timeout=10, text=True, encoding='utf-8')
    print(result.stderr, end='')
    return 'Error code: 404' in result.stdout


assert test_https_server(HTTPS_SERVER_HOSTNAME,
                         HTTP_SERVER_PORT), 'https server not up'


def start_naive(naive_args):
    with_qemu = None
    if argv.target_cpu == 'arm64':
        with_qemu = 'aarch64'
    elif argv.target_cpu == 'arm':
        with_qemu = 'arm'
    elif argv.target_cpu == 'mipsel':
        with_qemu = 'mipsel'
    elif argv.target_cpu == 'mips64el':
        with_qemu = 'mips64el'

    if argv.rootfs:
        if not with_qemu:
            if not os.path.exists(os.path.join(argv.rootfs, 'naive')):
                shutil.copy2(argv.naive, argv.rootfs)
            cmdline = ['bwrap', '--die-with-parent', '--bind', argv.rootfs, '/',
                       '--proc', '/proc', '--dev', '/dev', '/naive']
        else:
            cmdline = [f'qemu-{with_qemu}', '-L', argv.rootfs, argv.naive]
    else:
        cmdline = [argv.naive]
    cmdline.extend(naive_args)

    proc = subprocess.Popen(cmdline, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE, text=True, encoding='utf-8')
    print('subprocess.Popen', ' '.join(cmdline), 'pid:', proc.pid)

    def terminate(proc):
        print('proc has timed out')
        print('terminate pid', proc.pid)
        proc.terminate()

    timeout = threading.Timer(10, terminate, args=(proc,))
    timeout.start()
    while True:
        if proc.poll() is not None:
            timeout.cancel()
            return proc.poll() == 0

        line = proc.stderr.readline().strip()
        print(line)
        if 'Failed to listen: ' in line:
            timeout.cancel()
            print('terminate pid', proc.pid)
            proc.terminate()
            return 'Failed to listen'
        elif 'Listening on ' in line:
            timeout.cancel()
            return proc


port = 10000


def allocate_port_number():
    global port
    port += 1
    if port > 60000:
        port = 10000
    return port


def test_naive_once(proxy, *args, **kwargs):
    port_map = {}

    class PortDict(dict):
        def __init__(self, port_map):
            self._port_map = port_map

        def __getitem__(self, key):
            if key.startswith('PORT'):
                if key not in self._port_map:
                    self._port_map[key] = str(allocate_port_number())
                return self._port_map[key]
            return key
    port_dict = PortDict(port_map)

    proxy = proxy.format_map(port_dict)

    config_file = kwargs.get('config_file', 'config.json')
    if argv.rootfs:
        config_file = os.path.join(argv.rootfs, config_file)
    config_content = kwargs.get('config_content')
    if config_content is not None:
        config_content = config_content.format_map(port_dict)
        with open(config_file, 'w') as f:
            f.write('{')
            f.write(config_content)
            f.write('}')

    naive_procs = []

    def cleanup():
        if config_content is not None:
            os.remove(config_file)
        for naive_proc in naive_procs:
            print('terminate pid', naive_proc.pid)
            naive_proc.terminate()

    for args_instance in args:
        naive_args = args_instance.format_map(port_dict).split()
        naive_proc = start_naive(naive_args)
        if naive_proc == 'Failed to listen':
            cleanup()
            return 'Failed to listen'
        if not naive_proc:
            cleanup()
            return False
        naive_procs.append(naive_proc)

    result = test_https_server(HTTPS_SERVER_HOSTNAME, HTTP_SERVER_PORT, proxy)

    cleanup()

    return result


def test_naive(label, proxy, *args, **kwargs):
    RETRIES = 5
    for i in range(RETRIES):
        result = test_naive_once(proxy, *args, **kwargs)
        if result == 'Failed to listen':
            print('Retrying...')
            time.sleep(1)
            continue
        if result:
            print('** TEST PASS:', label, end='\n\n')
            return True
        return result
    print('** TEST FAIL:', label, end='\n\n')
    os.exit(1)


test_naive('Default config', 'socks5h://127.0.0.1:1080',
           '--log')

test_naive('Default config file', 'socks5h://127.0.0.1:{PORT1}',
           '',
           config_content='"listen":"socks://127.0.0.1:{PORT1}","log":""')

test_naive('Custom config file', 'socks5h://127.0.0.1:{PORT1}',
           'custom.json',
           config_content='"listen":"socks://127.0.0.1:{PORT1}","log":""',
           config_file='custom.json')

test_naive('Trivial - listen scheme only', 'socks5h://127.0.0.1:1080',
           '--log --listen=socks://')

test_naive('Trivial - listen no host', 'socks5h://127.0.0.1:{PORT1}',
           '--log --listen=socks://:{PORT1}')

test_naive('Trivial - listen no port', 'socks5h://127.0.0.1:1080',
           '--log --listen=socks://127.0.0.1')

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
           '--log --listen=socks://:1080')

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
