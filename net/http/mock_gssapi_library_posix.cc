// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/mock_gssapi_library_posix.h"

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace test {

struct GssNameMockImpl {
  std::string name;
  gss_OID_desc name_type;
};

}  // namespace test

namespace {

// gss_OID helpers.
// NOTE: gss_OID's do not own the data they point to, which should be static.
void ClearOid(gss_OID dest) {
  if (!dest)
    return;
  dest->length = 0;
  dest->elements = NULL;
}

void SetOid(gss_OID dest, const void* src, size_t length) {
  if (!dest)
    return;
  ClearOid(dest);
  if (!src)
    return;
  dest->length = length;
  if (length)
    dest->elements = const_cast<void*>(src);
}

void CopyOid(gss_OID dest, const gss_OID_desc* src) {
  if (!dest)
    return;
  ClearOid(dest);
  if (!src)
    return;
  SetOid(dest, src->elements, src->length);
}

// gss_buffer_t helpers.
void ClearBuffer(gss_buffer_t dest) {
  if (!dest)
    return;
  dest->length = 0;
  delete [] reinterpret_cast<char*>(dest->value);
  dest->value = NULL;
}

void SetBuffer(gss_buffer_t dest, const void* src, size_t length) {
  if (!dest)
    return;
  ClearBuffer(dest);
  if (!src)
    return;
  dest->length = length;
  if (length) {
    dest->value = new char[length];
    memcpy(dest->value, src, length);
  }
}

void CopyBuffer(gss_buffer_t dest, const gss_buffer_t src) {
  if (!dest)
    return;
  ClearBuffer(dest);
  if (!src)
    return;
  SetBuffer(dest, src->value, src->length);
}

std::string BufferToString(const gss_buffer_t src) {
  std::string dest;
  if (!src)
    return dest;
  const char* string = reinterpret_cast<char*>(src->value);
  dest.assign(string, src->length);
  return dest;
}

void BufferFromString(const std::string& src, gss_buffer_t dest) {
  if (!dest)
    return;
  SetBuffer(dest, src.c_str(), src.length());
}

// gss_name_t helpers.
void ClearName(gss_name_t dest) {
  if (!dest)
    return;
  test::GssNameMockImpl* name = reinterpret_cast<test::GssNameMockImpl*>(dest);
  name->name.clear();
  ClearOid(&name->name_type);
}

void SetName(gss_name_t dest, const void* src, size_t length) {
  if (!dest)
    return;
  ClearName(dest);
  if (!src)
    return;
  test::GssNameMockImpl* name = reinterpret_cast<test::GssNameMockImpl*>(dest);
  name->name.assign(reinterpret_cast<const char*>(src), length);
}

std::string NameToString(const gss_name_t& src) {
  std::string dest;
  if (!src)
    return dest;
  test::GssNameMockImpl* string =
      reinterpret_cast<test::GssNameMockImpl*>(src);
  dest = string->name;
  return dest;
}

void NameFromString(const std::string& src, gss_name_t dest) {
  if (!dest)
    return;
  SetName(dest, src.c_str(), src.length());
}

}  // namespace

namespace test {

GssContextMockImpl::GssContextMockImpl()
  : lifetime_rec(0),
    ctx_flags(0),
    locally_initiated(0),
    open(0) {
  ClearOid(&mech_type);
}

GssContextMockImpl::GssContextMockImpl(const GssContextMockImpl& other)
  : src_name(other.src_name),
    targ_name(other.targ_name),
    lifetime_rec(other.lifetime_rec),
    ctx_flags(other.ctx_flags),
    locally_initiated(other.locally_initiated),
    open(other.open) {
  CopyOid(&mech_type, &other.mech_type);
}

GssContextMockImpl::GssContextMockImpl(const char* src_name_in,
                                       const char* targ_name_in,
                                       OM_uint32 lifetime_rec_in,
                                       const gss_OID_desc& mech_type_in,
                                       OM_uint32 ctx_flags_in,
                                       int locally_initiated_in,
                                       int open_in)
    : src_name(src_name_in ? src_name_in : ""),
      targ_name(targ_name_in ? targ_name_in : ""),
      lifetime_rec(lifetime_rec_in),
      ctx_flags(ctx_flags_in),
      locally_initiated(locally_initiated_in),
      open(open_in) {
  CopyOid(&mech_type, &mech_type_in);
}

GssContextMockImpl::~GssContextMockImpl() {
  ClearOid(&mech_type);
}

void GssContextMockImpl::Assign(
    const GssContextMockImpl& other) {
  if (&other == this)
    return;
  src_name = other.src_name;
  targ_name = other.targ_name;
  lifetime_rec = other.lifetime_rec;
  CopyOid(&mech_type, &other.mech_type);
  ctx_flags = other.ctx_flags;
  locally_initiated = other.locally_initiated;
  open = other.open;
}

MockGSSAPILibrary::SecurityContextQuery::SecurityContextQuery()
    : expected_package(),
      response_code(0),
      minor_response_code(0),
      context_info() {
  expected_input_token.length = 0;
  expected_input_token.value = NULL;
  output_token.length = 0;
  output_token.value = NULL;
}

MockGSSAPILibrary::SecurityContextQuery::SecurityContextQuery(
    const std::string& in_expected_package,
    OM_uint32 in_response_code,
    OM_uint32 in_minor_response_code,
    const test::GssContextMockImpl& in_context_info,
    const char* in_expected_input_token,
    const char* in_output_token)
    : expected_package(in_expected_package),
      response_code(in_response_code),
      minor_response_code(in_minor_response_code),
      context_info(in_context_info) {
  if (in_expected_input_token) {
    expected_input_token.length = strlen(in_expected_input_token);
    expected_input_token.value = const_cast<char*>(in_expected_input_token);
  } else {
    expected_input_token.length = 0;
    expected_input_token.value = NULL;
  }

  if (in_output_token) {
    output_token.length = strlen(in_output_token);
    output_token.value = const_cast<char*>(in_output_token);
  } else {
    output_token.length = 0;
    output_token.value = NULL;
  }
}

MockGSSAPILibrary::SecurityContextQuery::SecurityContextQuery(
    const SecurityContextQuery& other) = default;

MockGSSAPILibrary::SecurityContextQuery::~SecurityContextQuery() {}

MockGSSAPILibrary::MockGSSAPILibrary() {
}

MockGSSAPILibrary::~MockGSSAPILibrary() {
}

void MockGSSAPILibrary::ExpectSecurityContext(
    const std::string& expected_package,
    OM_uint32 response_code,
    OM_uint32 minor_response_code,
    const GssContextMockImpl& context_info,
    const gss_buffer_desc& expected_input_token,
    const gss_buffer_desc& output_token) {
  SecurityContextQuery security_query;
  security_query.expected_package = expected_package;
  security_query.response_code = response_code;
  security_query.minor_response_code = minor_response_code;
  security_query.context_info.Assign(context_info);
  security_query.expected_input_token = expected_input_token;
  security_query.output_token = output_token;
  expected_security_queries_.push_back(security_query);
}

bool MockGSSAPILibrary::Init() {
  return true;
}

// These methods match the ones in the GSSAPI library.
OM_uint32 MockGSSAPILibrary::import_name(
      OM_uint32* minor_status,
      const gss_buffer_t input_name_buffer,
      const gss_OID input_name_type,
      gss_name_t* output_name) {
  if (minor_status)
    *minor_status = 0;
  if (!output_name)
    return GSS_S_BAD_NAME;
  if (!input_name_buffer)
    return GSS_S_CALL_BAD_STRUCTURE;
  if (!input_name_type)
    return GSS_S_BAD_NAMETYPE;
  GssNameMockImpl* output = new GssNameMockImpl;
  if (output == NULL)
    return GSS_S_FAILURE;
  output->name_type.length = 0;
  output->name_type.elements = NULL;

  // Save the data.
  output->name = BufferToString(input_name_buffer);
  CopyOid(&output->name_type, input_name_type);
  *output_name = reinterpret_cast<gss_name_t>(output);

  return GSS_S_COMPLETE;
}

OM_uint32 MockGSSAPILibrary::release_name(
      OM_uint32* minor_status,
      gss_name_t* input_name) {
  if (minor_status)
    *minor_status = 0;
  if (!input_name)
    return GSS_S_BAD_NAME;
  if (!*input_name)
    return GSS_S_COMPLETE;
  GssNameMockImpl* name = *reinterpret_cast<GssNameMockImpl**>(input_name);
  ClearName(*input_name);
  delete name;
  *input_name = NULL;
  return GSS_S_COMPLETE;
}

OM_uint32 MockGSSAPILibrary::release_buffer(
      OM_uint32* minor_status,
      gss_buffer_t buffer) {
  if (minor_status)
    *minor_status = 0;
  if (!buffer)
    return GSS_S_BAD_NAME;
  ClearBuffer(buffer);
  return GSS_S_COMPLETE;
}

OM_uint32 MockGSSAPILibrary::display_name(
    OM_uint32* minor_status,
    const gss_name_t input_name,
    gss_buffer_t output_name_buffer,
    gss_OID* output_name_type) {
  if (minor_status)
    *minor_status = 0;
  if (!input_name)
    return GSS_S_BAD_NAME;
  if (!output_name_buffer)
    return GSS_S_CALL_BAD_STRUCTURE;
  if (!output_name_type)
    return GSS_S_CALL_BAD_STRUCTURE;
  std::string name(NameToString(input_name));
  BufferFromString(name, output_name_buffer);
  GssNameMockImpl* internal_name =
      *reinterpret_cast<GssNameMockImpl**>(input_name);
  if (output_name_type)
    *output_name_type = internal_name ? &internal_name->name_type : NULL;
  return GSS_S_COMPLETE;
}

OM_uint32 MockGSSAPILibrary::display_status(
      OM_uint32* minor_status,
      OM_uint32 status_value,
      int status_type,
      const gss_OID mech_type,
      OM_uint32* message_context,
      gss_buffer_t status_string) {
  if (minor_status)
    *minor_status = 0;
  std::string msg = base::StringPrintf("Value: %u, Type %u",
                                       status_value,
                                       status_type);
  if (message_context)
    *message_context = 0;
  BufferFromString(msg, status_string);
  return GSS_S_COMPLETE;
}

OM_uint32 MockGSSAPILibrary::init_sec_context(
      OM_uint32* minor_status,
      const gss_cred_id_t initiator_cred_handle,
      gss_ctx_id_t* context_handle,
      const gss_name_t target_name,
      const gss_OID mech_type,
      OM_uint32 req_flags,
      OM_uint32 time_req,
      const gss_channel_bindings_t input_chan_bindings,
      const gss_buffer_t input_token,
      gss_OID* actual_mech_type,
      gss_buffer_t output_token,
      OM_uint32* ret_flags,
      OM_uint32* time_rec) {
  if (minor_status)
    *minor_status = 0;
  if (!context_handle)
    return GSS_S_CALL_BAD_STRUCTURE;
  GssContextMockImpl** internal_context_handle =
      reinterpret_cast<test::GssContextMockImpl**>(context_handle);
  // Create it if necessary.
  if (!*internal_context_handle) {
    *internal_context_handle = new GssContextMockImpl;
  }
  EXPECT_TRUE(*internal_context_handle);
  GssContextMockImpl& context = **internal_context_handle;
  if (expected_security_queries_.empty()) {
    return GSS_S_UNAVAILABLE;
  }
  SecurityContextQuery security_query = expected_security_queries_.front();
  expected_security_queries_.pop_front();
  EXPECT_EQ(std::string("Negotiate"), security_query.expected_package);
  OM_uint32 major_status = security_query.response_code;
  if (minor_status)
    *minor_status = security_query.minor_response_code;
  context.src_name = security_query.context_info.src_name;
  context.targ_name = security_query.context_info.targ_name;
  context.lifetime_rec = security_query.context_info.lifetime_rec;
  CopyOid(&context.mech_type, &security_query.context_info.mech_type);
  context.ctx_flags = security_query.context_info.ctx_flags;
  context.locally_initiated = security_query.context_info.locally_initiated;
  context.open = security_query.context_info.open;
  if (!input_token) {
    EXPECT_FALSE(security_query.expected_input_token.length);
  } else {
    EXPECT_EQ(input_token->length, security_query.expected_input_token.length);
    if (input_token->length) {
      EXPECT_EQ(0, memcmp(input_token->value,
                          security_query.expected_input_token.value,
                          input_token->length));
    }
  }
  CopyBuffer(output_token, &security_query.output_token);
  if (actual_mech_type)
    CopyOid(*actual_mech_type, mech_type);
  if (ret_flags)
    *ret_flags = req_flags;
  return major_status;
}

OM_uint32 MockGSSAPILibrary::wrap_size_limit(
      OM_uint32* minor_status,
      const gss_ctx_id_t context_handle,
      int conf_req_flag,
      gss_qop_t qop_req,
      OM_uint32 req_output_size,
      OM_uint32* max_input_size) {
  if (minor_status)
    *minor_status = 0;
  ADD_FAILURE();
  return GSS_S_UNAVAILABLE;
}

OM_uint32 MockGSSAPILibrary::delete_sec_context(
      OM_uint32* minor_status,
      gss_ctx_id_t* context_handle,
      gss_buffer_t output_token) {
  if (minor_status)
    *minor_status = 0;
  if (!context_handle)
    return GSS_S_CALL_BAD_STRUCTURE;
  GssContextMockImpl** internal_context_handle =
      reinterpret_cast<GssContextMockImpl**>(context_handle);
  if (*internal_context_handle) {
    delete *internal_context_handle;
    *internal_context_handle = NULL;
  }
  return GSS_S_COMPLETE;
}

OM_uint32 MockGSSAPILibrary::inquire_context(
    OM_uint32* minor_status,
    const gss_ctx_id_t context_handle,
    gss_name_t* src_name,
    gss_name_t* targ_name,
    OM_uint32* lifetime_rec,
    gss_OID* mech_type,
    OM_uint32* ctx_flags,
    int* locally_initiated,
    int* open) {
  if (minor_status)
    *minor_status = 0;
  if (!context_handle)
    return GSS_S_CALL_BAD_STRUCTURE;
  GssContextMockImpl* internal_context_ptr =
      reinterpret_cast<GssContextMockImpl*>(context_handle);
  GssContextMockImpl& context = *internal_context_ptr;
  if (src_name)
    NameFromString(context.src_name, *src_name);
  if (targ_name)
    NameFromString(context.targ_name, *targ_name);
  if (lifetime_rec)
    *lifetime_rec = context.lifetime_rec;
  if (mech_type)
    CopyOid(*mech_type, &context.mech_type);
  if (ctx_flags)
    *ctx_flags = context.ctx_flags;
  if (locally_initiated)
    *locally_initiated = context.locally_initiated;
  if (open)
    *open = context.open;
  return GSS_S_COMPLETE;
}

}  // namespace test

}  // namespace net
