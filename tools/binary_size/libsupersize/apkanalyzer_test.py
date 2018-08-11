#!/usr/bin/env python
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import apkanalyzer


class ApkAnalyzerTest(unittest.TestCase):
  def assertEqualLists(self, list1, list2):
    self.assertEqual(set(list1), set(list2))

  def testUndoHierarchicalSizing_Empty(self):
    data = []
    nodes = apkanalyzer.UndoHierarchicalSizing(data)
    self.assertEqual(0, len(nodes))

  def testUndoHierarchicalSizing_TotalSingleRootNode(self):
    data = [
      ('<TOTAL>', 5),
    ]
    nodes = apkanalyzer.UndoHierarchicalSizing(data)
    # No changes expected since there are no child nodes.
    self.assertEqualLists(data, nodes)

  def testUndoHierarchicalSizing_TotalSizeMinusChildNode(self):
    data = [
      ('<TOTAL>', 10),
      ('child1', 7),
    ]
    nodes = apkanalyzer.UndoHierarchicalSizing(data)
    self.assertEqualLists([
      ('<TOTAL>', 3),
      ('child1', 7),
    ], nodes)

  def testUndoHierarchicalSizing_SiblingAnonymousClass(self):
    data = [
      ('class1', 10),
      ('class1$inner', 8),
    ]
    nodes = apkanalyzer.UndoHierarchicalSizing(data)
    # No change in size expected since these should be siblings.
    self.assertEqualLists(data, nodes)

  def testUndoHierarchicalSizing_MethodsShouldBeChildNodes(self):
    data = [
      ('class1', 10),
      ('class1 method', 8),
    ]
    nodes = apkanalyzer.UndoHierarchicalSizing(data)
    self.assertEqualLists([
      ('class1', 2),
      ('class1 method', 8),
    ], nodes)

  def testUndoHierarchicalSizing_ClassIsChildNodeOfPackage(self):
    data = [
      ('package1', 10),
      ('package1.class1', 3),
    ]
    nodes = apkanalyzer.UndoHierarchicalSizing(data)
    self.assertEqualLists([
      ('package1', 7),
      ('package1.class1', 3),
    ], nodes)

  def testUndoHierarchicalSizing_TotalIncludesAllPackages(self):
    data = [
      ('<TOTAL>', 10),
      ('package1', 3),
      ('package2', 4),
      ('package3', 2),
    ]
    nodes = apkanalyzer.UndoHierarchicalSizing(data)
    self.assertEqualLists([
      ('<TOTAL>', 1),
      ('package1', 3),
      ('package2', 4),
      ('package3', 2),
    ], nodes)


if __name__ == '__main__':
  unittest.main()
