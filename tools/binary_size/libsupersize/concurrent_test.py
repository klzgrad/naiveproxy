#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import threading
import unittest

import concurrent


def _ForkTestHelper(test_instance, parent_pid, arg1, arg2, _=None):
  test_instance.assertNotEquals(os.getpid(), parent_pid)
  return arg1 + arg2


class Unpicklable(object):
  """Ensures that pickle() is not called on parameters."""
  def __getstate__(self):
    raise AssertionError('Tried to pickle')


class ConcurrentTest(unittest.TestCase):
  def testEncodeDictOfLists_Empty(self):
    test_dict = {}
    encoded = concurrent.EncodeDictOfLists(test_dict)
    decoded = concurrent.DecodeDictOfLists(encoded)
    self.assertEquals(test_dict, decoded)

  def testEncodeDictOfLists_EmptyValue(self):
    test_dict = {'foo': []}
    encoded = concurrent.EncodeDictOfLists(test_dict)
    decoded = concurrent.DecodeDictOfLists(encoded)
    self.assertEquals(test_dict, decoded)

  def testEncodeDictOfLists_AllStrings(self):
    test_dict = {'foo': ['a', 'b', 'c'], 'foo2': ['a', 'b']}
    encoded = concurrent.EncodeDictOfLists(test_dict)
    decoded = concurrent.DecodeDictOfLists(encoded)
    self.assertEquals(test_dict, decoded)

  def testEncodeDictOfLists_KeyTransform(self):
    test_dict = {0: ['a', 'b', 'c'], 9: ['a', 'b']}
    encoded = concurrent.EncodeDictOfLists(test_dict, key_transform=str)
    decoded = concurrent.DecodeDictOfLists(encoded, key_transform=int)
    self.assertEquals(test_dict, decoded)

  def testEncodeDictOfLists_ValueTransform(self):
    test_dict = {'a': ['0', '1', '2'], 'b': ['3', '4']}
    expected = {'a': [0, 1, 2], 'b': [3, 4]}
    encoded = concurrent.EncodeDictOfLists(test_dict)
    decoded = concurrent.DecodeDictOfLists(encoded, value_transform=int)
    self.assertEquals(expected, decoded)

  def testEncodeDictOfLists_Join_Empty(self):
    test_dict1 = {}
    test_dict2 = {}
    expected = {}
    encoded1 = concurrent.EncodeDictOfLists(test_dict1)
    encoded2 = concurrent.EncodeDictOfLists(test_dict2)
    encoded = concurrent.JoinEncodedDictOfLists([encoded1, encoded2])
    decoded = concurrent.DecodeDictOfLists(encoded)
    self.assertEquals(expected, decoded)

  def testEncodeDictOfLists_Join_Singl(self):
    test_dict1 = {'key1': ['a']}
    encoded1 = concurrent.EncodeDictOfLists(test_dict1)
    encoded = concurrent.JoinEncodedDictOfLists([encoded1])
    decoded = concurrent.DecodeDictOfLists(encoded)
    self.assertEquals(test_dict1, decoded)

  def testEncodeDictOfLists_JoinMultiple(self):
    test_dict1 = {'key1': ['a']}
    test_dict2 = {'key2': ['b']}
    expected = {'key1': ['a'], 'key2': ['b']}
    encoded1 = concurrent.EncodeDictOfLists(test_dict1)
    encoded2 = concurrent.EncodeDictOfLists({})
    encoded3 = concurrent.EncodeDictOfLists(test_dict2)
    encoded = concurrent.JoinEncodedDictOfLists([encoded1, encoded2, encoded3])
    decoded = concurrent.DecodeDictOfLists(encoded)
    self.assertEquals(expected, decoded)

  def testCallOnThread(self):
    main_thread = threading.current_thread()
    def callback(arg1, arg2):
      self.assertEquals(1, arg1)
      self.assertEquals(2, arg2)
      my_thread = threading.current_thread()
      self.assertNotEquals(my_thread, main_thread)
      return 3

    result = concurrent.CallOnThread(callback, 1, arg2=2)
    self.assertEquals(3, result.get())

  def testForkAndCall_normal(self):
    parent_pid = os.getpid()
    result = concurrent.ForkAndCall(
        _ForkTestHelper, (self, parent_pid, 1, 2, Unpicklable()))
    self.assertEquals(3, result.get())

  def testForkAndCall_exception(self):
    parent_pid = os.getpid()
    result = concurrent.ForkAndCall(_ForkTestHelper, (self, parent_pid, 1, 'a'))
    self.assertRaises(TypeError, result.get)

  def testBulkForkAndCall_none(self):
    results = concurrent.BulkForkAndCall(_ForkTestHelper, [])
    self.assertEquals([], list(results))

  def testBulkForkAndCall_few(self):
    parent_pid = os.getpid()
    results = concurrent.BulkForkAndCall(_ForkTestHelper, [
        (self, parent_pid, 1, 2, Unpicklable()),
        (self, parent_pid, 3, 4)])
    self.assertEquals({3, 7}, set(results))

  def testBulkForkAndCall_many(self):
    parent_pid = os.getpid()
    args = [(self, parent_pid, 1, 2, Unpicklable())] * 100
    results = concurrent.BulkForkAndCall(_ForkTestHelper, args)
    self.assertEquals([3] * 100, list(results))

  def testBulkForkAndCall_exception(self):
    parent_pid = os.getpid()
    results = concurrent.BulkForkAndCall(_ForkTestHelper, [
        (self, parent_pid, 1, 'a')])
    self.assertRaises(TypeError, results.next)

if __name__ == '__main__':
  unittest.main()
