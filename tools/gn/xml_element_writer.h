// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_XML_ELEMENT_WRITER_H_
#define TOOLS_GN_XML_ELEMENT_WRITER_H_

#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_piece.h"

// Vector of XML attribute key-value pairs.
class XmlAttributes
    : public std::vector<std::pair<base::StringPiece, base::StringPiece>> {
 public:
  XmlAttributes();
  XmlAttributes(const base::StringPiece& attr_key,
                const base::StringPiece& attr_value);

  XmlAttributes& add(const base::StringPiece& attr_key,
                     const base::StringPiece& attr_value);
};

// Helper class for writing XML elements. New XML element is started in
// XmlElementWriter constructor and ended in its destructor. XmlElementWriter
// handles XML file formatting in order to produce human-readable document.
class XmlElementWriter {
 public:
  // Starts new XML element. This constructor adds no indentation and is
  // designed for XML root element.
  XmlElementWriter(std::ostream& out,
                   const std::string& tag,
                   const XmlAttributes& attributes);
  // Starts new XML element with specified indentation.
  XmlElementWriter(std::ostream& out,
                   const std::string& tag,
                   const XmlAttributes& attributes,
                   int indent);
  // Starts new XML element with specified indentation. Specialized constructor
  // that allows writting XML element with single attribute without copying
  // attribute value.
  template <class Writer>
  XmlElementWriter(std::ostream& out,
                   const std::string& tag,
                   const std::string& attribute_name,
                   const Writer& attribute_value_writer,
                   int indent);
  // Ends XML element. All sub-elements should be ended at this point.
  ~XmlElementWriter();

  // Writes arbitrary XML element text.
  void Text(const base::StringPiece& content);

  // Starts new XML sub-element. Caller must ensure that parent element outlives
  // its children.
  std::unique_ptr<XmlElementWriter> SubElement(const std::string& tag);
  std::unique_ptr<XmlElementWriter> SubElement(const std::string& tag,
                                               const XmlAttributes& attributes);
  template <class Writer>
  std::unique_ptr<XmlElementWriter> SubElement(
      const std::string& tag,
      const std::string& attribute_name,
      const Writer& attribute_value_writer);

  // Finishes opening tag if it isn't finished yet and optionally starts new
  // document line. Returns the stream where XML element content can be written.
  // This is an alternative to Text() and SubElement() methods.
  std::ostream& StartContent(bool start_new_line);

 private:
  // Output stream. XmlElementWriter objects for XML element and its
  // sub-elements share the same output stream.
  std::ostream& out_;

  // XML element tag name.
  std::string tag_;

  // XML element indentation in the document.
  int indent_;

  // Flag indicating if opening tag is finished with '>' character already.
  bool opening_tag_finished_;

  // Flag indicating if XML element should be written in one document line.
  bool one_line_;

  DISALLOW_COPY_AND_ASSIGN(XmlElementWriter);
};

template <class Writer>
XmlElementWriter::XmlElementWriter(std::ostream& out,
                                   const std::string& tag,
                                   const std::string& attribute_name,
                                   const Writer& attribute_value_writer,
                                   int indent)
    : out_(out),
      tag_(tag),
      indent_(indent),
      opening_tag_finished_(false),
      one_line_(true) {
  out << std::string(indent, ' ') << '<' << tag;
  out << ' ' << attribute_name << "=\"";
  attribute_value_writer(out);
  out << '\"';
}

template <class Writer>
std::unique_ptr<XmlElementWriter> XmlElementWriter::SubElement(
    const std::string& tag,
    const std::string& attribute_name,
    const Writer& attribute_value_writer) {
  StartContent(true);
  return base::MakeUnique<XmlElementWriter>(
      out_, tag, attribute_name, attribute_value_writer, indent_ + 2);
}

std::string XmlEscape(const std::string& value);

#endif  // TOOLS_GN_XML_ELEMENT_WRITER_H_
