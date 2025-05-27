# Copyright 2017 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A "Test Server Spawner" that handles killing/stopping per-test test servers.

It's used to accept requests from the device to spawn and kill instances of the
chrome test server on the host.
"""
# pylint: disable=W0702

import json
import logging
import os
import select
import struct
import subprocess
import sys
import threading
import time
import urllib

from http.server import BaseHTTPRequestHandler
from http.server import HTTPServer

SERVER_TYPES = {
    'http': '',
    'ftp': '-f',
    'ws': '--websocket',
}


_DIR_SOURCE_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir, os.pardir,
                 os.pardir))


_logger = logging.getLogger(__name__)


# Path that are needed to import necessary modules when launching a testserver.
os.environ['PYTHONPATH'] = os.environ.get('PYTHONPATH', '') + (
    ':%s:%s' % (os.path.join(_DIR_SOURCE_ROOT, 'third_party'),
                os.path.join(_DIR_SOURCE_ROOT, 'net', 'tools', 'testserver')))


def _GetServerTypeCommandLine(server_type):
  """Returns the command-line by the given server type.

  Args:
    server_type: the server type to be used (e.g. 'http').

  Returns:
    A string containing the command-line argument.
  """
  if server_type not in SERVER_TYPES:
    raise NotImplementedError('Unknown server type: %s' % server_type)
  return SERVER_TYPES[server_type]


class PortForwarder:
  def Map(self, port_pairs):
    pass

  def GetDevicePortForHostPort(self, host_port):
    """Returns the device port that corresponds to a given host port."""
    return host_port

  def WaitHostPortAvailable(self, port):
    """Returns True if |port| is available."""
    return True

  def WaitPortNotAvailable(self, port):
    """Returns True if |port| is not available."""
    return True

  def WaitDevicePortReady(self, port):
    """Returns whether the provided port is used."""
    return True

  def Unmap(self, device_port):
    """Unmaps specified port"""
    pass


class TestServerThread(threading.Thread):
  """A thread to run the test server in a separate process."""

  def __init__(self, ready_event, arguments, port_forwarder):
    """Initialize TestServerThread with the following argument.

    Args:
      ready_event: event which will be set when the test server is ready.
      arguments: dictionary of arguments to run the test server.
      device: An instance of DeviceUtils.
      tool: instance of runtime error detection tool.
    """
    threading.Thread.__init__(self)
    self.wait_event = threading.Event()
    self.stop_event = threading.Event()
    self.ready_event = ready_event
    self.ready_event.clear()
    self.arguments = arguments
    self.port_forwarder = port_forwarder
    self.test_server_process = None
    self.is_ready = False
    self.host_port = 0
    self.host_ocsp_port = 0
    assert isinstance(self.host_port, int)
    # The forwarder device port now is dynamically allocated.
    self.forwarder_device_port = 0
    self.forwarder_ocsp_device_port = 0
    self.process = None
    self.command_line = []

  def _WaitToStartAndGetPortFromTestServer(self, pipe_in):
    """Waits for the Python test server to start and gets the port it is using.

    The port information is passed by the Python test server with a pipe given
    by |pipe_in|. It is written as a result to |self.host_port|.

    Returns:
      Whether the port used by the test server was successfully fetched.
    """
    (in_fds, _, _) = select.select([pipe_in], [], [])
    if len(in_fds) == 0:
      _logger.error('Failed to wait to the Python test server to be started.')
      return False
    # First read the data length as an unsigned 4-byte value.  This
    # is _not_ using network byte ordering since the Python test server packs
    # size as native byte order and all Chromium platforms so far are
    # configured to use little-endian.
    # TODO(jnd): Change the Python test server and local_test_server_*.cc to
    # use a unified byte order (either big-endian or little-endian).
    data_length = os.read(pipe_in, struct.calcsize('=L'))
    if data_length:
      (data_length,) = struct.unpack('=L', data_length)
      assert data_length
    if not data_length:
      _logger.error('Failed to get length of server data.')
      return False
    server_data_json = os.read(pipe_in, data_length)
    if not server_data_json:
      _logger.error('Failed to get server data.')
      return False
    _logger.info('Got port json data: %s', server_data_json)

    parsed_server_data = None
    try:
      parsed_server_data = json.loads(server_data_json)
    except ValueError:
      pass

    if not isinstance(parsed_server_data, dict):
      _logger.error('Failed to parse server_data: %s' % server_data_json)
      return False

    if not isinstance(parsed_server_data.get('port'), int):
      _logger.error('Failed to get port information from the server data.')
      return False

    self.host_port = parsed_server_data['port']
    self.host_ocsp_port = parsed_server_data.get('ocsp_port', 0)

    return self.port_forwarder.WaitPortNotAvailable(self.host_port)

  def _GenerateCommandLineArguments(self, pipe_out):
    """Generates the command line to run the test server.

    Note that all options are processed by following the definitions in
    testserver.py.
    """
    if self.command_line:
      return

    args_copy = dict(self.arguments)

    # Translate the server type.
    type_cmd = _GetServerTypeCommandLine(args_copy.pop('server-type'))
    if type_cmd:
      self.command_line.append(type_cmd)

    # Use a pipe to get the port given by the Python test server.
    self.command_line.append('--startup-pipe=%d' % pipe_out)

    # Pass the remaining arguments as-is.
    for key, values in args_copy.items():
      if not isinstance(values, list):
        values = [values]
      for value in values:
        if value is None:
          self.command_line.append('--%s' % key)
        else:
          self.command_line.append('--%s=%s' % (key, value))

  def _CloseUnnecessaryFDsForTestServerProcess(self, pipe_out):
    # This is required to avoid subtle deadlocks that could be caused by the
    # test server child process inheriting undesirable file descriptors such as
    # file lock file descriptors. Note stdin, stdout, and stderr (0-2) are left
    # alone and redirected with subprocess.Popen. It is important to leave those
    # fds filled, or the test server will accidentally open other fds at those
    # numbers.
    for fd in range(3, 1024):
      if fd != pipe_out:
        try:
          os.close(fd)
        except:
          pass

  def run(self):
    _logger.info('Start running the thread!')
    self.wait_event.clear()

    # Set up a pipe for the server to report when it has started.
    pipe_in, pipe_out = os.pipe()

    # TODO(crbug.com/40618161): Remove if condition after python3 migration.
    if hasattr(os, 'set_inheritable'):
      os.set_inheritable(pipe_out, True)

    try:
      self._GenerateCommandLineArguments(pipe_out)
      # TODO(crbug.com/40618161): When this script is ported to Python 3, replace
      # 'vpython3' below with sys.executable.
      command = [
          'vpython3',
          os.path.join(_DIR_SOURCE_ROOT, 'net', 'tools', 'testserver',
                       'testserver.py')
      ] + self.command_line
      _logger.info('Running: %s', command)

      # Disable PYTHONUNBUFFERED because it has a bad interaction with the
      # testserver. Remove once this interaction is fixed.
      unbuf = os.environ.pop('PYTHONUNBUFFERED', None)

      # Pass _DIR_SOURCE_ROOT as the child's working directory so that relative
      # paths in the arguments are resolved correctly. devnull can be replaced
      # with subprocess.DEVNULL in Python 3.
      with open(os.devnull, 'r+b') as devnull:
        self.process = subprocess.Popen(
            command,
            preexec_fn=lambda: self._CloseUnnecessaryFDsForTestServerProcess(
                pipe_out),
            stdin=devnull,
            # Preserve stdout and stderr from the test server.
            stdout=None,
            stderr=None,
            cwd=_DIR_SOURCE_ROOT,
            close_fds=False)

      # Close pipe_out early. If self.process crashes, this will be visible
      # in _WaitToStartAndGetPortFromTestServer's select loop.
      os.close(pipe_out)
      pipe_out = -1
      if unbuf:
        os.environ['PYTHONUNBUFFERED'] = unbuf
      self.is_ready = self._WaitToStartAndGetPortFromTestServer(pipe_in)

      if self.is_ready:
        port_map = [(0, self.host_port)]
        if self.host_ocsp_port:
          port_map.extend([(0, self.host_ocsp_port)])
        self.port_forwarder.Map(port_map)

        self.forwarder_device_port = \
            self.port_forwarder.GetDevicePortForHostPort(self.host_port)
        if self.host_ocsp_port:
          self.forwarder_ocsp_device_port = \
              self.port_forwarder.GetDevicePortForHostPort(self.host_ocsp_port)

        # Check whether the forwarder is ready on the device.
        self.is_ready = self.forwarder_device_port and \
            self.port_forwarder.WaitDevicePortReady(self.forwarder_device_port)

      # Wake up the request handler thread.
      self.ready_event.set()
      # Keep thread running until Stop() gets called.
      self.stop_event.wait()
      if self.process.poll() is None:
        self.process.kill()
        # Wait for process to actually terminate.
        # (crbug.com/946475)
        self.process.wait()

      self.port_forwarder.Unmap(self.forwarder_device_port)
      self.process = None
      self.is_ready = False
    finally:
      if pipe_in >= 0:
        os.close(pipe_in)
      if pipe_out >= 0:
        os.close(pipe_out)
    _logger.info('Test-server has died.')
    self.wait_event.set()

  def Stop(self):
    """Blocks until the loop has finished.

    Note that this must be called in another thread.
    """
    if not self.process:
      return
    self.stop_event.set()
    self.wait_event.wait()


class SpawningServerRequestHandler(BaseHTTPRequestHandler):
  """A handler used to process http GET/POST request."""

  def _SendResponse(self, response_code, response_reason, additional_headers,
                    contents):
    """Generates a response sent to the client from the provided parameters.

    Args:
      response_code: number of the response status.
      response_reason: string of reason description of the response.
      additional_headers: dict of additional headers. Each key is the name of
                          the header, each value is the content of the header.
      contents: string of the contents we want to send to client.
    """
    self.send_response(response_code, response_reason)
    self.send_header('Content-Type', 'text/html')
    # Specify the content-length as without it the http(s) response will not
    # be completed properly (and the browser keeps expecting data).
    self.send_header('Content-Length', len(contents))
    for header_name in additional_headers:
      self.send_header(header_name, additional_headers[header_name])
    self.end_headers()
    self.wfile.write(contents.encode('utf8'))
    self.wfile.flush()

  def _StartTestServer(self):
    """Starts the test server thread."""
    _logger.info('Handling request to spawn a test server.')
    content_type = self.headers.get('content-type')
    if content_type != 'application/json':
      raise Exception('Bad content-type for start request.')
    content_length = self.headers.get('content-length')
    if not content_length:
      content_length = 0
    try:
      content_length = int(content_length)
    except:
      raise Exception('Bad content-length for start request.')
    _logger.info(content_length)
    test_server_argument_json = self.rfile.read(content_length)
    _logger.info(test_server_argument_json)

    if len(self.server.test_servers) >= self.server.max_instances:
      self._SendResponse(400, 'Invalid request', {},
                         'Too many test servers running')
      return

    ready_event = threading.Event()
    new_server = TestServerThread(ready_event,
                                  json.loads(test_server_argument_json),
                                  self.server.port_forwarder)
    new_server.setDaemon(True)
    new_server.start()
    ready_event.wait()
    if new_server.is_ready:
      response = {'port': new_server.forwarder_device_port,
                  'message': 'started'};
      if new_server.forwarder_ocsp_device_port:
        response['ocsp_port'] = new_server.forwarder_ocsp_device_port
      self._SendResponse(200, 'OK', {}, json.dumps(response))
      _logger.info('Test server is running on port %d forwarded to %d.' %
              (new_server.forwarder_device_port, new_server.host_port))
      port = new_server.forwarder_device_port
      assert port not in self.server.test_servers
      self.server.test_servers[port] = new_server
    else:
      new_server.Stop()
      self._SendResponse(500, 'Test Server Error.', {}, '')
      _logger.info('Encounter problem during starting a test server.')

  def _KillTestServer(self, params):
    """Stops the test server instance."""
    try:
      port = int(params['port'][0])
    except ValueError:
      port = None
    if port == None or port <= 0:
      self._SendResponse(400, 'Invalid request.', {}, 'port must be specified')
      return

    if port not in self.server.test_servers:
      self._SendResponse(400, 'Invalid request.', {},
                         "testserver isn't running on port %d" % port)
      return

    server = self.server.test_servers.pop(port)

    _logger.info('Handling request to kill a test server on port: %d.', port)
    server.Stop()

    # Make sure the status of test server is correct before sending response.
    if self.server.port_forwarder.WaitHostPortAvailable(port):
      self._SendResponse(200, 'OK', {}, 'killed')
      _logger.info('Test server on port %d is killed', port)
    else:
      # We expect the port to be free, but nothing stops the system from
      # binding something else to that port, so don't throw error.
      # (crbug.com/946475)
      self._SendResponse(200, 'OK', {}, '')
      _logger.warn('Port %s is not free after killing test server.' % port)

  def log_message(self, format, *args):
    # Suppress the default HTTP logging behavior if the logging level is higher
    # than INFO.
    if _logger.getEffectiveLevel() <= logging.INFO:
      pass

  def do_POST(self):
    parsed_path = urllib.parse.urlparse(self.path)
    action = parsed_path.path
    _logger.info('Action for POST method is: %s.', action)
    if action == '/start':
      self._StartTestServer()
    else:
      self._SendResponse(400, 'Unknown request.', {}, '')
      _logger.info('Encounter unknown request: %s.', action)

  def do_GET(self):
    parsed_path = urllib.parse.urlparse(self.path)
    action = parsed_path.path
    params = urllib.parse.parse_qs(parsed_path.query, keep_blank_values=1)
    _logger.info('Action for GET method is: %s.', action)
    for param in params:
      _logger.info('%s=%s', param, params[param][0])
    if action == '/kill':
      self._KillTestServer(params)
    elif action == '/ping':
      # The ping handler is used to check whether the spawner server is ready
      # to serve the requests. We don't need to test the status of the test
      # server when handling ping request.
      self._SendResponse(200, 'OK', {}, 'ready')
      _logger.info('Handled ping request and sent response.')
    else:
      self._SendResponse(400, 'Unknown request', {}, '')
      _logger.info('Encounter unknown request: %s.', action)


class SpawningServer(object):
  """The class used to start/stop a http server."""

  def __init__(self, test_server_spawner_port, port_forwarder, max_instances):
    self.server = HTTPServer(('', test_server_spawner_port),
                             SpawningServerRequestHandler)
    self.server_port = self.server.server_port
    _logger.info('Started test server spawner on port: %d.', self.server_port)

    self.server.port_forwarder = port_forwarder
    self.server.test_servers = {}
    self.server.max_instances = max_instances

  def _Listen(self):
    _logger.info('Starting test server spawner.')
    self.server.serve_forever()

  def Start(self):
    """Starts the test server spawner."""
    listener_thread = threading.Thread(target=self._Listen)
    listener_thread.setDaemon(True)
    listener_thread.start()

  def Stop(self):
    """Stops the test server spawner.

    Also cleans the server state.
    """
    self.CleanupState()
    self.server.shutdown()

  def CleanupState(self):
    """Cleans up the spawning server state.

    This should be called if the test server spawner is reused,
    to avoid sharing the test server instance.
    """
    if self.server.test_servers:
      _logger.warning('Not all test servers were stopped.')
      for port in self.server.test_servers:
        _logger.warning('Stopping test server on port %d' % port)
        self.server.test_servers[port].Stop()
      self.server.test_servers = {}
