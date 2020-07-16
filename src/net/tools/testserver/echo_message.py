# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Provides utility functions for TCP/UDP echo servers and clients.

This program has classes and functions to encode, decode, calculate checksum
and verify the "echo request" and "echo response" messages. "echo request"
message is an echo message sent from the client to the server. "echo response"
message is a response from the server to the "echo request" message from the
client.

The format of "echo request" message is
<version><checksum><payload_size><payload>. <version> is the version number
of the "echo request" protocol. <checksum> is the checksum of the <payload>.
<payload_size> is the size of the <payload>. <payload> is the echo message.

The format of "echo response" message is
<version><checksum><payload_size><key><encoded_payload>.<version>,
<checksum> and <payload_size> are same as what is in the "echo request" message.
<encoded_payload> is encoded version of the <payload>. <key> is a randomly
generated key that is used to encode/decode the <payload>.
"""

__author__ = 'rtenneti@google.com (Raman Tenneti)'


from itertools import cycle
from itertools import izip
import random


class EchoHeader(object):
  """Class to keep header info of the EchoRequest and EchoResponse messages.

  This class knows how to parse the checksum, payload_size from the
  "echo request" and "echo response" messages. It holds the checksum,
  payload_size of the "echo request" and "echo response" messages.
  """

  # This specifies the version.
  VERSION_STRING = '01'

  # This specifies the starting position of the checksum and length of the
  # checksum. Maximum value for the checksum is less than (2 ** 31 - 1).
  CHECKSUM_START = 2
  CHECKSUM_LENGTH = 10
  CHECKSUM_FORMAT = '%010d'
  CHECKSUM_END = CHECKSUM_START + CHECKSUM_LENGTH

  # This specifies the starting position of the <payload_size> and length of the
  # <payload_size>. Maximum number of bytes that can be sent in the <payload> is
  # 9,999,999.
  PAYLOAD_SIZE_START = CHECKSUM_END
  PAYLOAD_SIZE_LENGTH = 7
  PAYLOAD_SIZE_FORMAT = '%07d'
  PAYLOAD_SIZE_END = PAYLOAD_SIZE_START + PAYLOAD_SIZE_LENGTH

  def __init__(self, checksum=0, payload_size=0):
    """Initializes the checksum and payload_size of self (EchoHeader).

    Args:
      checksum: (int)
        The checksum of the payload.
      payload_size: (int)
        The size of the payload.
    """
    self.checksum = checksum
    self.payload_size = payload_size

  def ParseAndInitialize(self, echo_message):
    """Parses the echo_message and initializes self with the parsed data.

    This method extracts checksum, and payload_size from the echo_message
    (echo_message could be either echo_request or echo_response messages) and
    initializes self (EchoHeader) with checksum and payload_size.

    Args:
      echo_message: (string)
        The string representation of EchoRequest or EchoResponse objects.
    Raises:
      ValueError: Invalid data
    """
    if not echo_message or len(echo_message) < EchoHeader.PAYLOAD_SIZE_END:
      raise ValueError('Invalid data:%s' % echo_message)
    self.checksum = int(echo_message[
        EchoHeader.CHECKSUM_START:EchoHeader.CHECKSUM_END])
    self.payload_size = int(echo_message[
        EchoHeader.PAYLOAD_SIZE_START:EchoHeader.PAYLOAD_SIZE_END])

  def InitializeFromPayload(self, payload):
    """Initializes the EchoHeader object with the payload.

    It calculates checksum for the payload and initializes self (EchoHeader)
    with the calculated checksum and size of the payload.

    This method is used by the client code during testing.

    Args:
      payload: (string)
        The payload is the echo string (like 'hello').
    Raises:
      ValueError: Invalid data
    """
    if not payload:
      raise ValueError('Invalid data:%s' % payload)
    self.payload_size = len(payload)
    self.checksum = Checksum(payload, self.payload_size)

  def __str__(self):
    """String representation of the self (EchoHeader).

    Returns:
      A string representation of self (EchoHeader).
    """
    checksum_string = EchoHeader.CHECKSUM_FORMAT % self.checksum
    payload_size_string = EchoHeader.PAYLOAD_SIZE_FORMAT % self.payload_size
    return EchoHeader.VERSION_STRING + checksum_string + payload_size_string


class EchoRequest(EchoHeader):
  """Class holds data specific to the "echo request" message.

  This class holds the payload extracted from the "echo request" message.
  """

  # This specifies the starting position of the <payload>.
  PAYLOAD_START = EchoHeader.PAYLOAD_SIZE_END

  def __init__(self):
    """Initializes EchoRequest object."""
    EchoHeader.__init__(self)
    self.payload = ''

  def ParseAndInitialize(self, echo_request_data):
    """Parses and Initializes the EchoRequest object from the echo_request_data.

    This method extracts the header information (checksum and payload_size) and
    payload from echo_request_data.

    Args:
      echo_request_data: (string)
        The string representation of EchoRequest object.
    Raises:
      ValueError: Invalid data
    """
    EchoHeader.ParseAndInitialize(self, echo_request_data)
    if len(echo_request_data) <= EchoRequest.PAYLOAD_START:
      raise ValueError('Invalid data:%s' % echo_request_data)
    self.payload = echo_request_data[EchoRequest.PAYLOAD_START:]

  def InitializeFromPayload(self, payload):
    """Initializes the EchoRequest object with payload.

    It calculates checksum for the payload and initializes self (EchoRequest)
    object.

    Args:
      payload: (string)
        The payload string for which "echo request" needs to be constructed.
    """
    EchoHeader.InitializeFromPayload(self, payload)
    self.payload = payload

  def __str__(self):
    """String representation of the self (EchoRequest).

    Returns:
      A string representation of self (EchoRequest).
    """
    return EchoHeader.__str__(self) + self.payload


class EchoResponse(EchoHeader):
  """Class holds data specific to the "echo response" message.

  This class knows how to parse the "echo response" message. This class holds
  key, encoded_payload and decoded_payload of the "echo response" message.
  """

  # This specifies the starting position of the |key_| and length of the |key_|.
  # Minimum and maximum values for the |key_| are 100,000 and 999,999.
  KEY_START = EchoHeader.PAYLOAD_SIZE_END
  KEY_LENGTH = 6
  KEY_FORMAT = '%06d'
  KEY_END = KEY_START + KEY_LENGTH
  KEY_MIN_VALUE = 0
  KEY_MAX_VALUE = 999999

  # This specifies the starting position of the <encoded_payload> and length
  # of the <encoded_payload>.
  ENCODED_PAYLOAD_START = KEY_END

  def __init__(self, key='', encoded_payload='', decoded_payload=''):
    """Initializes the EchoResponse object."""
    EchoHeader.__init__(self)
    self.key = key
    self.encoded_payload = encoded_payload
    self.decoded_payload = decoded_payload

  def ParseAndInitialize(self, echo_response_data=None):
    """Parses and Initializes the EchoResponse object from echo_response_data.

    This method calls EchoHeader to extract header information from the
    echo_response_data and it then extracts key and encoded_payload from the
    echo_response_data. It holds the decoded payload of the encoded_payload.

    Args:
      echo_response_data: (string)
        The string representation of EchoResponse object.
    Raises:
      ValueError: Invalid echo_request_data
    """
    EchoHeader.ParseAndInitialize(self, echo_response_data)
    if len(echo_response_data) <= EchoResponse.ENCODED_PAYLOAD_START:
      raise ValueError('Invalid echo_response_data:%s' % echo_response_data)
    self.key = echo_response_data[EchoResponse.KEY_START:EchoResponse.KEY_END]
    self.encoded_payload = echo_response_data[
        EchoResponse.ENCODED_PAYLOAD_START:]
    self.decoded_payload = Crypt(self.encoded_payload, self.key)

  def InitializeFromEchoRequest(self, echo_request):
    """Initializes EchoResponse with the data from the echo_request object.

    It gets the checksum, payload_size and payload from the echo_request object
    and then encodes the payload with a random key. It also saves the payload
    as decoded_payload.

    Args:
      echo_request: (EchoRequest)
        The EchoRequest object which has "echo request" message.
    """
    self.checksum = echo_request.checksum
    self.payload_size = echo_request.payload_size
    self.key = (EchoResponse.KEY_FORMAT %
                random.randrange(EchoResponse.KEY_MIN_VALUE,
                                 EchoResponse.KEY_MAX_VALUE))
    self.encoded_payload = Crypt(echo_request.payload, self.key)
    self.decoded_payload = echo_request.payload

  def __str__(self):
    """String representation of the self (EchoResponse).

    Returns:
      A string representation of self (EchoResponse).
    """
    return EchoHeader.__str__(self) + self.key + self.encoded_payload


def Crypt(payload, key):
  """Encodes/decodes the payload with the key and returns encoded payload.

  This method loops through the payload and XORs each byte with the key.

  Args:
    payload: (string)
      The string to be encoded/decoded.
    key: (string)
      The key used to encode/decode the payload.

  Returns:
    An encoded/decoded string.
  """
  return ''.join(chr(ord(x) ^ ord(y)) for (x, y) in izip(payload, cycle(key)))


def Checksum(payload, payload_size):
  """Calculates the checksum of the payload.

  Args:
    payload: (string)
      The payload string for which checksum needs to be calculated.
    payload_size: (int)
      The number of bytes in the payload.

  Returns:
    The checksum of the payload.
  """
  checksum = 0
  length = min(payload_size, len(payload))
  for i in range (0, length):
    checksum += ord(payload[i])
  return checksum


def GetEchoRequestData(payload):
  """Constructs an "echo request" message from the payload.

  It builds an EchoRequest object from the payload and then returns a string
  representation of the EchoRequest object.

  This is used by the TCP/UDP echo clients to build the "echo request" message.

  Args:
    payload: (string)
      The payload string for which "echo request" needs to be constructed.

  Returns:
    A string representation of the EchoRequest object.
  Raises:
    ValueError: Invalid payload
  """
  try:
    echo_request = EchoRequest()
    echo_request.InitializeFromPayload(payload)
    return str(echo_request)
  except (IndexError, ValueError):
    raise ValueError('Invalid payload:%s' % payload)


def GetEchoResponseData(echo_request_data):
  """Verifies the echo_request_data and returns "echo response" message.

  It builds the EchoRequest object from the echo_request_data and then verifies
  the checksum of the EchoRequest is same as the calculated checksum of the
  payload. If the checksums don't match then it returns None. It checksums
  match, it builds the echo_response object from echo_request object and returns
  string representation of the EchoResponse object.

  This is used by the TCP/UDP echo servers.

  Args:
    echo_request_data: (string)
      The string that echo servers send to the clients.

  Returns:
    A string representation of the EchoResponse object. It returns None if the
    echo_request_data is not valid.
  Raises:
    ValueError: Invalid echo_request_data
  """
  try:
    if not echo_request_data:
      raise ValueError('Invalid payload:%s' % echo_request_data)

    echo_request = EchoRequest()
    echo_request.ParseAndInitialize(echo_request_data)

    if Checksum(echo_request.payload,
                echo_request.payload_size) != echo_request.checksum:
      return None

    echo_response = EchoResponse()
    echo_response.InitializeFromEchoRequest(echo_request)

    return str(echo_response)
  except (IndexError, ValueError):
    raise ValueError('Invalid payload:%s' % echo_request_data)


def DecodeAndVerify(echo_request_data, echo_response_data):
  """Decodes and verifies the echo_response_data.

  It builds EchoRequest and EchoResponse objects from the echo_request_data and
  echo_response_data. It returns True if the EchoResponse's payload and
  checksum match EchoRequest's.

  This is used by the TCP/UDP echo clients for testing purposes.

  Args:
    echo_request_data: (string)
      The request clients sent to echo servers.
    echo_response_data: (string)
      The response clients received from the echo servers.

  Returns:
    True if echo_request_data and echo_response_data match.
  Raises:
    ValueError: Invalid echo_request_data or Invalid echo_response
  """

  try:
    echo_request = EchoRequest()
    echo_request.ParseAndInitialize(echo_request_data)
  except (IndexError, ValueError):
    raise ValueError('Invalid echo_request:%s' % echo_request_data)

  try:
    echo_response = EchoResponse()
    echo_response.ParseAndInitialize(echo_response_data)
  except (IndexError, ValueError):
    raise ValueError('Invalid echo_response:%s' % echo_response_data)

  return (echo_request.checksum == echo_response.checksum and
          echo_request.payload == echo_response.decoded_payload)
