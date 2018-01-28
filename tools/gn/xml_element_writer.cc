// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/xml_element_writer.h"

#include <memory>

XmlAttributes::XmlAttributes() = default;

XmlAttributes::XmlAttributes(const base::StringPiece& attr_key,
                             const base::StringPiece& attr_value) {
  add(attr_key, attr_value);
}

XmlAttributes& XmlAttributes::add(const base::StringPiece& attr_key,
                                  const base::StringPiece& attr_value) {
  push_back(std::make_pair(attr_key, attr_value));
  return *this;
}

XmlElementWriter::XmlElementWriter(std::ostream& out,
                                   const std::string& tag,
                                   const XmlAttributes& attributes)
    : XmlElementWriter(out, tag, attributes, 0) {}

XmlElementWriter::XmlElementWriter(std::ostream& out,
                                   const std::string& tag,
                                   const XmlAttributes& attributes,
                                   int indent)
    : out_(out),
      tag_(tag),
      indent_(indent),
      opening_tag_finished_(false),
      one_line_(true) {
  out << std::string(indent, ' ') << '<' << tag;
  for (auto attribute : attributes)
    out << ' ' << attribute.first << "=\"" << attribute.second << '"';
}

XmlElementWriter::~XmlElementWriter() {
  if (!opening_tag_finished_) {
    // The XML spec does not require a space before the closing slash. However,
    // Eclipse is unable to parse XML settings files if there is no space.
    out_ << " />" << std::endl;
  } else {
    if (!one_line_)
      out_ << std::string(indent_, ' ');
    out_ << "</" << tag_ << '>' << std::endl;
  }
}

void XmlElementWriter::Text(const base::StringPiece& content) {
  StartContent(false);
  out_ << content;
}

std::unique_ptr<XmlElementWriter> XmlElementWriter::SubElement(
    const std::string& tag) {
  return SubElement(tag, XmlAttributes());
}

std::unique_ptr<XmlElementWriter> XmlElementWriter::SubElement(
    const std::string& tag,
    const XmlAttributes& attributes) {
  StartContent(true);
  return std::make_unique<XmlElementWriter>(out_, tag, attributes, indent_ + 2);
}

std::ostream& XmlElementWriter::StartContent(bool start_new_line) {
  if (!opening_tag_finished_) {
    out_ << '>';
    opening_tag_finished_ = true;

    if (start_new_line && one_line_) {
      out_ << std::endl;
      one_line_ = false;
    }
  }

  return out_;
}

std::string XmlEscape(const std::string& value) {
  std::string result;
  for (char c : value) {
    switch (c) {
      case '\n':
        result += "&#10;";
        break;
      case '\r':
        result += "&#13;";
        break;
      case '\t':
        result += "&#9;";
        break;
      case '"':
        result += "&quot;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      case '&':
        result += "&amp;";
        break;
      default:
        result += c;
    }
  }
  return result;
}
