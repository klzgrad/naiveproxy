#!/usr/bin/env python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Unit test suite that collects all test cases for GRIT.'''

import os
import sys
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

import unittest


# TODO(joi) Use unittest.defaultTestLoader to automatically load tests
# from modules. Iterating over the directory and importing could then
# automate this all the way, if desired.


class TestSuiteAll(unittest.TestSuite):
  def __init__(self):
    super(TestSuiteAll, self).__init__()
    # Imports placed here to prevent circular imports.
    # pylint: disable-msg=C6204
    import grit.clique_unittest
    import grit.grd_reader_unittest
    import grit.grit_runner_unittest
    import grit.lazy_re_unittest
    import grit.shortcuts_unittests
    import grit.tclib_unittest
    import grit.util_unittest
    import grit.xtb_reader_unittest
    import grit.format.android_xml_unittest
    import grit.format.c_format_unittest
    import grit.format.chrome_messages_json_unittest
    import grit.format.data_pack_unittest
    import grit.format.gzip_string_unittest
    import grit.format.html_inline_unittest
    import grit.format.js_map_format_unittest
    import grit.format.policy_templates_json_unittest
    import grit.format.rc_header_unittest
    import grit.format.rc_unittest
    import grit.format.resource_map_unittest
    import grit.gather.admin_template_unittest
    import grit.gather.chrome_html_unittest
    import grit.gather.chrome_scaled_image_unittest
    import grit.gather.policy_json_unittest
    import grit.gather.rc_unittest
    import grit.gather.tr_html_unittest
    import grit.gather.txt_unittest
    import grit.node.base_unittest
    import grit.node.io_unittest
    import grit.node.include_unittest
    import grit.node.message_unittest
    import grit.node.misc_unittest
    import grit.node.structure_unittest #
    import grit.node.custom.filename_unittest
    import grit.tool.android2grd_unittest
    import grit.tool.build_unittest
    import grit.tool.buildinfo_unittest
    import grit.tool.postprocess_unittest
    import grit.tool.preprocess_unittest
    import grit.tool.rc2grd_unittest
    import grit.tool.transl2tc_unittest
    import grit.tool.xmb_unittest

    test_classes = [
        grit.clique_unittest.MessageCliqueUnittest,
        grit.grd_reader_unittest.GrdReaderUnittest,
        grit.grit_runner_unittest.OptionArgsUnittest,
        grit.lazy_re_unittest.LazyReUnittest,
        grit.shortcuts_unittests.ShortcutsUnittest,
        grit.tclib_unittest.TclibUnittest,
        grit.util_unittest.UtilUnittest,
        grit.xtb_reader_unittest.XtbReaderUnittest,
        grit.format.android_xml_unittest.AndroidXmlUnittest,
        grit.format.c_format_unittest.CFormatUnittest,
        grit.format.chrome_messages_json_unittest.
            ChromeMessagesJsonFormatUnittest,
        grit.format.data_pack_unittest.FormatDataPackUnittest,
        grit.format.gzip_string_unittest.FormatGzipStringUnittest,
        grit.format.html_inline_unittest.HtmlInlineUnittest,
        grit.format.js_map_format_unittest.JsMapFormatUnittest,
        grit.format.policy_templates_json_unittest.PolicyTemplatesJsonUnittest,
        grit.format.rc_header_unittest.RcHeaderFormatterUnittest,
        grit.format.rc_unittest.FormatRcUnittest,
        grit.format.resource_map_unittest.FormatResourceMapUnittest,
        grit.gather.admin_template_unittest.AdmGathererUnittest,
        grit.gather.chrome_html_unittest.ChromeHtmlUnittest,
        grit.gather.chrome_scaled_image_unittest.ChromeScaledImageUnittest,
        grit.gather.policy_json_unittest.PolicyJsonUnittest,
        grit.gather.rc_unittest.RcUnittest,
        grit.gather.tr_html_unittest.ParserUnittest,
        grit.gather.tr_html_unittest.TrHtmlUnittest,
        grit.gather.txt_unittest.TxtUnittest,
        grit.node.base_unittest.NodeUnittest,
        grit.node.io_unittest.FileNodeUnittest,
        grit.node.include_unittest.IncludeNodeUnittest,
        grit.node.message_unittest.MessageUnittest,
        grit.node.misc_unittest.GritNodeUnittest,
        grit.node.misc_unittest.IfNodeUnittest,
        grit.node.misc_unittest.ReleaseNodeUnittest,
        grit.node.structure_unittest.StructureUnittest,
        grit.node.custom.filename_unittest.WindowsFilenameUnittest,
        grit.tool.android2grd_unittest.Android2GrdUnittest,
        grit.tool.build_unittest.BuildUnittest,
        grit.tool.buildinfo_unittest.BuildInfoUnittest,
        grit.tool.postprocess_unittest.PostProcessingUnittest,
        grit.tool.preprocess_unittest.PreProcessingUnittest,
        grit.tool.rc2grd_unittest.Rc2GrdUnittest,
        grit.tool.transl2tc_unittest.TranslationToTcUnittest,
        grit.tool.xmb_unittest.XmbUnittest,
        # add test classes here, in alphabetical order...
    ]

    for test_class in test_classes:
      self.addTest(unittest.makeSuite(test_class))


if __name__ == '__main__':
  test_result = unittest.TextTestRunner(verbosity=2).run(TestSuiteAll())
  sys.exit(len(test_result.errors) + len(test_result.failures))
