# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging

from devil.android import logcat_monitor
from devil.utils import reraiser_thread
from pylib.utils import logdog_helper

class LogdogLogcatMonitor(logcat_monitor.LogcatMonitor):
  """Logcat monitor that writes logcat to a logdog stream.

  The logdog stream client will return a url which contains the logcat.
  """
  def __init__(self, adb, stream_name, clear=True, filter_specs=None,
               deobfuscate_func=None):
    super(LogdogLogcatMonitor, self).__init__(adb, clear, filter_specs)
    self._logcat_url = ''
    self._logdog_stream = None
    self._stream_client = None
    self._stream_name = stream_name
    self._deobfuscate_func = deobfuscate_func or (lambda lines: lines)

  def GetLogcatURL(self):
    """Return logcat url.

    The default logcat url is '', if failed to create stream_client.
    """
    return self._logcat_url

  def Stop(self):
    """Stops the logcat monitor.

    Close the logdog stream as well.
    """
    try:
      super(LogdogLogcatMonitor, self)._StopRecording()
      if self._logdog_stream:
        self._logdog_stream.close()
    except Exception as e: # pylint: disable=broad-except
      logging.exception('Unknown Error: %s.', e)

  def Start(self):
    """Starts the logdog logcat monitor.

    Clears the logcat if |clear| was set in |__init__|.
    """
    if self._clear:
      self._adb.Logcat(clear=True)

    self._logdog_stream = logdog_helper.open_text(self._stream_name)
    self._logcat_url = logdog_helper.get_viewer_url(self._stream_name)
    logging.info('Logcat will be saved to %s', self._logcat_url)
    self._StartRecording()

  def _StartRecording(self):
    """Starts recording logcat to file.

    Write logcat to stream at the same time.
    """
    def record_to_stream():
      if self._logdog_stream:
        for data in self._adb.Logcat(filter_specs=self._filter_specs,
                                     logcat_format='threadtime',
                                     iter_timeout=0.08):
          if self._stop_recording_event.isSet():
            return
          if data:
            data = '\n'.join(self._deobfuscate_func([data]))
            self._logdog_stream.write(data + '\n')
          if self._stop_recording_event.isSet():
            return

    self._stop_recording_event.clear()
    if not self._record_thread:
      self._record_thread = reraiser_thread.ReraiserThread(record_to_stream)
      self._record_thread.start()

  def Close(self):
    """Override parent's close method."""
    pass

  def __del__(self):
    """Override parent's delete method."""
    pass
