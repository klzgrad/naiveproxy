// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_gssapi_posix.h"

#include <limits>
#include <string>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_multi_round_parse.h"

// These are defined for the GSSAPI library:
// Paraphrasing the comments from gssapi.h:
// "The implementation must reserve static storage for a
// gss_OID_desc object for each constant.  That constant
// should be initialized to point to that gss_OID_desc."
// These are encoded using ASN.1 BER encoding.
namespace {

static gss_OID_desc GSS_C_NT_USER_NAME_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x01")
};
static gss_OID_desc GSS_C_NT_MACHINE_UID_NAME_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x02")
};
static gss_OID_desc GSS_C_NT_STRING_UID_NAME_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x03")
};
static gss_OID_desc GSS_C_NT_HOSTBASED_SERVICE_X_VAL = {
  6,
  const_cast<char*>("\x2b\x06\x01\x05\x06\x02")
};
static gss_OID_desc GSS_C_NT_HOSTBASED_SERVICE_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x04")
};
static gss_OID_desc GSS_C_NT_ANONYMOUS_VAL = {
  6,
  const_cast<char*>("\x2b\x06\01\x05\x06\x03")
};
static gss_OID_desc GSS_C_NT_EXPORT_NAME_VAL = {
  6,
  const_cast<char*>("\x2b\x06\x01\x05\x06\x04")
};

}  // namespace

// Heimdal >= 1.4 will define the following as preprocessor macros.
// To avoid conflicting declarations, we have to undefine these.
#undef GSS_C_NT_USER_NAME
#undef GSS_C_NT_MACHINE_UID_NAME
#undef GSS_C_NT_STRING_UID_NAME
#undef GSS_C_NT_HOSTBASED_SERVICE_X
#undef GSS_C_NT_HOSTBASED_SERVICE
#undef GSS_C_NT_ANONYMOUS
#undef GSS_C_NT_EXPORT_NAME

gss_OID GSS_C_NT_USER_NAME = &GSS_C_NT_USER_NAME_VAL;
gss_OID GSS_C_NT_MACHINE_UID_NAME = &GSS_C_NT_MACHINE_UID_NAME_VAL;
gss_OID GSS_C_NT_STRING_UID_NAME = &GSS_C_NT_STRING_UID_NAME_VAL;
gss_OID GSS_C_NT_HOSTBASED_SERVICE_X = &GSS_C_NT_HOSTBASED_SERVICE_X_VAL;
gss_OID GSS_C_NT_HOSTBASED_SERVICE = &GSS_C_NT_HOSTBASED_SERVICE_VAL;
gss_OID GSS_C_NT_ANONYMOUS = &GSS_C_NT_ANONYMOUS_VAL;
gss_OID GSS_C_NT_EXPORT_NAME = &GSS_C_NT_EXPORT_NAME_VAL;

namespace net {

// Exported mechanism for GSSAPI. We always use SPNEGO:

// iso.org.dod.internet.security.mechanism.snego (1.3.6.1.5.5.2)
gss_OID_desc CHROME_GSS_SPNEGO_MECH_OID_DESC_VAL = {
  6,
  const_cast<char*>("\x2b\x06\x01\x05\x05\x02")
};

gss_OID CHROME_GSS_SPNEGO_MECH_OID_DESC =
    &CHROME_GSS_SPNEGO_MECH_OID_DESC_VAL;

// Debugging helpers.
namespace {

std::string DisplayStatus(OM_uint32 major_status,
                          OM_uint32 minor_status) {
  if (major_status == GSS_S_COMPLETE)
    return "OK";
  return base::StringPrintf("0x%08X 0x%08X", major_status, minor_status);
}

std::string DisplayCode(GSSAPILibrary* gssapi_lib,
                        OM_uint32 status,
                        OM_uint32 status_code_type) {
  const int kMaxDisplayIterations = 8;
  const size_t kMaxMsgLength = 4096;
  // msg_ctx needs to be outside the loop because it is invoked multiple times.
  OM_uint32 msg_ctx = 0;
  std::string rv = base::StringPrintf("(0x%08X)", status);

  // This loop should continue iterating until msg_ctx is 0 after the first
  // iteration. To be cautious and prevent an infinite loop, it stops after
  // a finite number of iterations as well. As an added sanity check, no
  // individual message may exceed |kMaxMsgLength|, and the final result
  // will not exceed |kMaxMsgLength|*2-1.
  for (int i = 0; i < kMaxDisplayIterations && rv.size() < kMaxMsgLength;
       ++i) {
    OM_uint32 min_stat;
    gss_buffer_desc_struct msg = GSS_C_EMPTY_BUFFER;
    OM_uint32 maj_stat =
        gssapi_lib->display_status(&min_stat, status, status_code_type,
                                   GSS_C_NULL_OID, &msg_ctx, &msg);
    if (maj_stat == GSS_S_COMPLETE) {
      int msg_len = (msg.length > kMaxMsgLength) ?
          static_cast<int>(kMaxMsgLength) :
          static_cast<int>(msg.length);
      if (msg_len > 0 && msg.value != NULL) {
        rv += base::StringPrintf(" %.*s", msg_len,
                                 static_cast<char*>(msg.value));
      }
    }
    gssapi_lib->release_buffer(&min_stat, &msg);
    if (!msg_ctx)
      break;
  }
  return rv;
}

std::string DisplayExtendedStatus(GSSAPILibrary* gssapi_lib,
                                  OM_uint32 major_status,
                                  OM_uint32 minor_status) {
  if (major_status == GSS_S_COMPLETE)
    return "OK";
  std::string major = DisplayCode(gssapi_lib, major_status, GSS_C_GSS_CODE);
  std::string minor = DisplayCode(gssapi_lib, minor_status, GSS_C_MECH_CODE);
  return base::StringPrintf("Major: %s | Minor: %s", major.c_str(),
                            minor.c_str());
}

// ScopedName releases a gss_name_t when it goes out of scope.
class ScopedName {
 public:
  ScopedName(gss_name_t name,
             GSSAPILibrary* gssapi_lib)
      : name_(name),
        gssapi_lib_(gssapi_lib) {
    DCHECK(gssapi_lib_);
  }

  ~ScopedName() {
    if (name_ != GSS_C_NO_NAME) {
      OM_uint32 minor_status = 0;
      OM_uint32 major_status =
          gssapi_lib_->release_name(&minor_status, &name_);
      if (major_status != GSS_S_COMPLETE) {
        LOG(WARNING) << "Problem releasing name. "
                     << DisplayStatus(major_status, minor_status);
      }
      name_ = GSS_C_NO_NAME;
    }
  }

 private:
  gss_name_t name_;
  GSSAPILibrary* gssapi_lib_;

  DISALLOW_COPY_AND_ASSIGN(ScopedName);
};

// ScopedBuffer releases a gss_buffer_t when it goes out of scope.
class ScopedBuffer {
 public:
  ScopedBuffer(gss_buffer_t buffer,
               GSSAPILibrary* gssapi_lib)
      : buffer_(buffer),
        gssapi_lib_(gssapi_lib) {
    DCHECK(gssapi_lib_);
  }

  ~ScopedBuffer() {
    if (buffer_ != GSS_C_NO_BUFFER) {
      OM_uint32 minor_status = 0;
      OM_uint32 major_status =
          gssapi_lib_->release_buffer(&minor_status, buffer_);
      if (major_status != GSS_S_COMPLETE) {
        LOG(WARNING) << "Problem releasing buffer. "
                     << DisplayStatus(major_status, minor_status);
      }
      buffer_ = GSS_C_NO_BUFFER;
    }
  }

 private:
  gss_buffer_t buffer_;
  GSSAPILibrary* gssapi_lib_;

  DISALLOW_COPY_AND_ASSIGN(ScopedBuffer);
};

namespace {

std::string AppendIfPredefinedValue(gss_OID oid,
                                    gss_OID predefined_oid,
                                    const char* predefined_oid_name) {
  DCHECK(oid);
  DCHECK(predefined_oid);
  DCHECK(predefined_oid_name);
  std::string output;
  if (oid->length != predefined_oid->length)
    return output;
  if (0 != memcmp(oid->elements,
                  predefined_oid->elements,
                  predefined_oid->length))
    return output;

  output += " (";
  output += predefined_oid_name;
  output += ")";
  return output;
}

}  // namespace

std::string DescribeOid(GSSAPILibrary* gssapi_lib, const gss_OID oid) {
  if (!oid)
    return "<NULL>";
  std::string output;
  const size_t kMaxCharsToPrint = 1024;
  OM_uint32 byte_length = oid->length;
  size_t char_length = byte_length / sizeof(char);
  if (char_length > kMaxCharsToPrint) {
    // This might be a plain ASCII string.
    // Check if the first |kMaxCharsToPrint| characters
    // contain only printable characters and are NULL terminated.
    const char* str = reinterpret_cast<const char*>(oid);
    size_t str_length = 0;
    for ( ; str_length < kMaxCharsToPrint; ++str_length) {
      if (!str[str_length] || !isprint(str[str_length]))
        break;
    }
    if (!str[str_length]) {
      output += base::StringPrintf("\"%s\"", str);
      return output;
    }
  }
  output = base::StringPrintf("(%u) \"", byte_length);
  if (!oid->elements) {
    output += "<NULL>";
    return output;
  }
  const unsigned char* elements =
      reinterpret_cast<const unsigned char*>(oid->elements);
  // Don't print more than |kMaxCharsToPrint| characters.
  size_t i = 0;
  for ( ; (i < byte_length) && (i < kMaxCharsToPrint); ++i) {
    output += base::StringPrintf("\\x%02X", elements[i]);
  }
  if (i >= kMaxCharsToPrint)
    output += "...";
  output += "\"";

  // Check if the OID is one of the predefined values.
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_USER_NAME,
                                    "GSS_C_NT_USER_NAME");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_MACHINE_UID_NAME,
                                    "GSS_C_NT_MACHINE_UID_NAME");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_STRING_UID_NAME,
                                    "GSS_C_NT_STRING_UID_NAME");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_HOSTBASED_SERVICE_X,
                                    "GSS_C_NT_HOSTBASED_SERVICE_X");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_HOSTBASED_SERVICE,
                                    "GSS_C_NT_HOSTBASED_SERVICE");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_ANONYMOUS,
                                    "GSS_C_NT_ANONYMOUS");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_EXPORT_NAME,
                                    "GSS_C_NT_EXPORT_NAME");

  return output;
}

std::string DescribeName(GSSAPILibrary* gssapi_lib, const gss_name_t name) {
  OM_uint32 major_status = 0;
  OM_uint32 minor_status = 0;
  gss_buffer_desc_struct output_name_buffer = GSS_C_EMPTY_BUFFER;
  gss_OID_desc output_name_type_desc = GSS_C_EMPTY_BUFFER;
  gss_OID output_name_type = &output_name_type_desc;
  major_status = gssapi_lib->display_name(&minor_status,
                                          name,
                                          &output_name_buffer,
                                          &output_name_type);
  ScopedBuffer scoped_output_name(&output_name_buffer, gssapi_lib);
  if (major_status != GSS_S_COMPLETE) {
    std::string error =
        base::StringPrintf("Unable to describe name 0x%p, %s",
                           name,
                           DisplayExtendedStatus(gssapi_lib,
                                                 major_status,
                                                 minor_status).c_str());
    return error;
  }
  int len = output_name_buffer.length;
  std::string description = base::StringPrintf(
      "%*s (Type %s)",
      len,
      reinterpret_cast<const char*>(output_name_buffer.value),
      DescribeOid(gssapi_lib, output_name_type).c_str());
  return description;
}

std::string DescribeContext(GSSAPILibrary* gssapi_lib,
                            const gss_ctx_id_t context_handle) {
  OM_uint32 major_status = 0;
  OM_uint32 minor_status = 0;
  gss_name_t src_name = GSS_C_NO_NAME;
  gss_name_t targ_name = GSS_C_NO_NAME;
  OM_uint32 lifetime_rec = 0;
  gss_OID mech_type = GSS_C_NO_OID;
  OM_uint32 ctx_flags = 0;
  int locally_initiated = 0;
  int open = 0;
  if (context_handle == GSS_C_NO_CONTEXT)
    return std::string("Context: GSS_C_NO_CONTEXT");
  major_status = gssapi_lib->inquire_context(&minor_status,
                                             context_handle,
                                             &src_name,
                                             &targ_name,
                                             &lifetime_rec,
                                             &mech_type,
                                             &ctx_flags,
                                             &locally_initiated,
                                             &open);
  ScopedName scoped_src_name(src_name, gssapi_lib);
  ScopedName scoped_targ_name(targ_name, gssapi_lib);
  if (major_status != GSS_S_COMPLETE) {
    std::string error =
        base::StringPrintf("Unable to describe context 0x%p, %s",
                           context_handle,
                           DisplayExtendedStatus(gssapi_lib,
                                                 major_status,
                                                 minor_status).c_str());
    return error;
  }
  std::string source(DescribeName(gssapi_lib, src_name));
  std::string target(DescribeName(gssapi_lib, targ_name));
  std::string description = base::StringPrintf("Context 0x%p: "
                                               "Source \"%s\", "
                                               "Target \"%s\", "
                                                "lifetime %d, "
                                                "mechanism %s, "
                                                "flags 0x%08X, "
                                                "local %d, "
                                                "open %d",
                                                context_handle,
                                                source.c_str(),
                                                target.c_str(),
                                                lifetime_rec,
                                                DescribeOid(gssapi_lib,
                                                            mech_type).c_str(),
                                                ctx_flags,
                                                locally_initiated,
                                                open);
  return description;
}

}  // namespace

GSSAPISharedLibrary::GSSAPISharedLibrary(const std::string& gssapi_library_name)
    : initialized_(false),
      gssapi_library_name_(gssapi_library_name),
      gssapi_library_(NULL),
      import_name_(NULL),
      release_name_(NULL),
      release_buffer_(NULL),
      display_name_(NULL),
      display_status_(NULL),
      init_sec_context_(NULL),
      wrap_size_limit_(NULL),
      delete_sec_context_(NULL),
      inquire_context_(NULL) {
}

GSSAPISharedLibrary::~GSSAPISharedLibrary() {
  if (gssapi_library_) {
    base::UnloadNativeLibrary(gssapi_library_);
    gssapi_library_ = NULL;
  }
}

bool GSSAPISharedLibrary::Init() {
  if (!initialized_)
    InitImpl();
  return initialized_;
}

bool GSSAPISharedLibrary::InitImpl() {
  DCHECK(!initialized_);
#if defined(DLOPEN_KERBEROS)
  gssapi_library_ = LoadSharedLibrary();
  if (gssapi_library_ == NULL)
    return false;
#endif  // defined(DLOPEN_KERBEROS)
  initialized_ = true;
  return true;
}

base::NativeLibrary GSSAPISharedLibrary::LoadSharedLibrary() {
  const char* const* library_names;
  size_t num_lib_names;
  const char* user_specified_library[1];
  if (!gssapi_library_name_.empty()) {
    user_specified_library[0] = gssapi_library_name_.c_str();
    library_names = user_specified_library;
    num_lib_names = 1;
  } else {
    static const char* const kDefaultLibraryNames[] = {
#if defined(OS_MACOSX)
      "/System/Library/Frameworks/GSS.framework/GSS"
#elif defined(OS_OPENBSD)
      "libgssapi.so"          // Heimdal - OpenBSD
#else
      "libgssapi_krb5.so.2",  // MIT Kerberos - FC, Suse10, Debian
      "libgssapi.so.4",       // Heimdal - Suse10, MDK
      "libgssapi.so.2",       // Heimdal - Gentoo
      "libgssapi.so.1"        // Heimdal - Suse9, CITI - FC, MDK, Suse10
#endif
    };
    library_names = kDefaultLibraryNames;
    num_lib_names = arraysize(kDefaultLibraryNames);
  }

  for (size_t i = 0; i < num_lib_names; ++i) {
    const char* library_name = library_names[i];
    base::FilePath file_path(library_name);

    // TODO(asanka): Move library loading to a separate thread.
    //               http://crbug.com/66702
    base::ThreadRestrictions::ScopedAllowIO allow_io_temporarily;
    base::NativeLibraryLoadError load_error;
    base::NativeLibrary lib = base::LoadNativeLibrary(file_path, &load_error);
    if (lib) {
      // Only return this library if we can bind the functions we need.
      if (BindMethods(lib))
        return lib;
      base::UnloadNativeLibrary(lib);
    } else {
      // If this is the only library available, log the reason for failure.
      LOG_IF(WARNING, num_lib_names == 1) << load_error.ToString();
    }
  }
  LOG(WARNING) << "Unable to find a compatible GSSAPI library";
  return NULL;
}

#if defined(DLOPEN_KERBEROS)
#define BIND(lib, x)                                                    \
  DCHECK(lib);                                                          \
  gss_##x##_type x = reinterpret_cast<gss_##x##_type>(                  \
      base::GetFunctionPointerFromNativeLibrary(lib, "gss_" #x));       \
  if (x == NULL) {                                                      \
    LOG(WARNING) << "Unable to bind function \"" << "gss_" #x << "\"";  \
    return false;                                                       \
  }
#else
#define BIND(lib, x) gss_##x##_type x = gss_##x
#endif

bool GSSAPISharedLibrary::BindMethods(base::NativeLibrary lib) {
  BIND(lib, import_name);
  BIND(lib, release_name);
  BIND(lib, release_buffer);
  BIND(lib, display_name);
  BIND(lib, display_status);
  BIND(lib, init_sec_context);
  BIND(lib, wrap_size_limit);
  BIND(lib, delete_sec_context);
  BIND(lib, inquire_context);

  import_name_ = import_name;
  release_name_ = release_name;
  release_buffer_ = release_buffer;
  display_name_ = display_name;
  display_status_ = display_status;
  init_sec_context_ = init_sec_context;
  wrap_size_limit_ = wrap_size_limit;
  delete_sec_context_ = delete_sec_context;
  inquire_context_ = inquire_context;

  return true;
}

#undef BIND

OM_uint32 GSSAPISharedLibrary::import_name(
    OM_uint32* minor_status,
    const gss_buffer_t input_name_buffer,
    const gss_OID input_name_type,
    gss_name_t* output_name) {
  DCHECK(initialized_);
  return import_name_(minor_status, input_name_buffer, input_name_type,
                      output_name);
}

OM_uint32 GSSAPISharedLibrary::release_name(
    OM_uint32* minor_status,
    gss_name_t* input_name) {
  DCHECK(initialized_);
  return release_name_(minor_status, input_name);
}

OM_uint32 GSSAPISharedLibrary::release_buffer(
    OM_uint32* minor_status,
    gss_buffer_t buffer) {
  DCHECK(initialized_);
  return release_buffer_(minor_status, buffer);
}

OM_uint32 GSSAPISharedLibrary::display_name(
    OM_uint32* minor_status,
    const gss_name_t input_name,
    gss_buffer_t output_name_buffer,
    gss_OID* output_name_type) {
  DCHECK(initialized_);
  return display_name_(minor_status,
                       input_name,
                       output_name_buffer,
                       output_name_type);
}

OM_uint32 GSSAPISharedLibrary::display_status(
    OM_uint32* minor_status,
    OM_uint32 status_value,
    int status_type,
    const gss_OID mech_type,
    OM_uint32* message_context,
    gss_buffer_t status_string) {
  DCHECK(initialized_);
  return display_status_(minor_status, status_value, status_type, mech_type,
                         message_context, status_string);
}

OM_uint32 GSSAPISharedLibrary::init_sec_context(
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
  DCHECK(initialized_);
  return init_sec_context_(minor_status,
                           initiator_cred_handle,
                           context_handle,
                           target_name,
                           mech_type,
                           req_flags,
                           time_req,
                           input_chan_bindings,
                           input_token,
                           actual_mech_type,
                           output_token,
                           ret_flags,
                           time_rec);
}

OM_uint32 GSSAPISharedLibrary::wrap_size_limit(
    OM_uint32* minor_status,
    const gss_ctx_id_t context_handle,
    int conf_req_flag,
    gss_qop_t qop_req,
    OM_uint32 req_output_size,
    OM_uint32* max_input_size) {
  DCHECK(initialized_);
  return wrap_size_limit_(minor_status,
                          context_handle,
                          conf_req_flag,
                          qop_req,
                          req_output_size,
                          max_input_size);
}

OM_uint32 GSSAPISharedLibrary::delete_sec_context(
    OM_uint32* minor_status,
    gss_ctx_id_t* context_handle,
    gss_buffer_t output_token) {
  // This is called from the owner class' destructor, even if
  // Init() is not called, so we can't assume |initialized_|
  // is set.
  if (!initialized_)
    return 0;
  return delete_sec_context_(minor_status,
                             context_handle,
                             output_token);
}

OM_uint32 GSSAPISharedLibrary::inquire_context(
    OM_uint32* minor_status,
    const gss_ctx_id_t context_handle,
    gss_name_t* src_name,
    gss_name_t* targ_name,
    OM_uint32* lifetime_rec,
    gss_OID* mech_type,
    OM_uint32* ctx_flags,
    int* locally_initiated,
    int* open) {
  DCHECK(initialized_);
  return inquire_context_(minor_status,
                          context_handle,
                          src_name,
                          targ_name,
                          lifetime_rec,
                          mech_type,
                          ctx_flags,
                          locally_initiated,
                          open);
}

ScopedSecurityContext::ScopedSecurityContext(GSSAPILibrary* gssapi_lib)
    : security_context_(GSS_C_NO_CONTEXT),
      gssapi_lib_(gssapi_lib) {
  DCHECK(gssapi_lib_);
}

ScopedSecurityContext::~ScopedSecurityContext() {
  if (security_context_ != GSS_C_NO_CONTEXT) {
    gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
    OM_uint32 minor_status = 0;
    OM_uint32 major_status = gssapi_lib_->delete_sec_context(
        &minor_status, &security_context_, &output_token);
    if (major_status != GSS_S_COMPLETE) {
      LOG(WARNING) << "Problem releasing security_context. "
                   << DisplayStatus(major_status, minor_status);
    }
    security_context_ = GSS_C_NO_CONTEXT;
  }
}

HttpAuthGSSAPI::HttpAuthGSSAPI(GSSAPILibrary* library,
                               const std::string& scheme,
                               gss_OID gss_oid)
    : scheme_(scheme),
      gss_oid_(gss_oid),
      library_(library),
      scoped_sec_context_(library),
      can_delegate_(false) {
  DCHECK(library_);
}

HttpAuthGSSAPI::~HttpAuthGSSAPI() = default;

bool HttpAuthGSSAPI::Init() {
  if (!library_)
    return false;
  return library_->Init();
}

bool HttpAuthGSSAPI::NeedsIdentity() const {
  return decoded_server_auth_token_.empty();
}

bool HttpAuthGSSAPI::AllowsExplicitCredentials() const {
  return false;
}

void HttpAuthGSSAPI::Delegate() {
  can_delegate_ = true;
}

HttpAuth::AuthorizationResult HttpAuthGSSAPI::ParseChallenge(
    HttpAuthChallengeTokenizer* tok) {
  if (scoped_sec_context_.get() == GSS_C_NO_CONTEXT) {
    return net::ParseFirstRoundChallenge(scheme_, tok);
  }
  std::string encoded_auth_token;
  return net::ParseLaterRoundChallenge(scheme_, tok, &encoded_auth_token,
                                       &decoded_server_auth_token_);
}

int HttpAuthGSSAPI::GenerateAuthToken(const AuthCredentials* credentials,
                                      const std::string& spn,
                                      const std::string& channel_bindings,
                                      std::string* auth_token,
                                      const CompletionCallback& /*callback*/) {
  DCHECK(auth_token);

  gss_buffer_desc input_token = GSS_C_EMPTY_BUFFER;
  input_token.length = decoded_server_auth_token_.length();
  input_token.value = (input_token.length > 0) ?
      const_cast<char*>(decoded_server_auth_token_.data()) :
      NULL;
  gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
  ScopedBuffer scoped_output_token(&output_token, library_);
  int rv =
      GetNextSecurityToken(spn, channel_bindings, &input_token, &output_token);
  if (rv != OK)
    return rv;

  // Base64 encode data in output buffer and prepend the scheme.
  std::string encode_input(static_cast<char*>(output_token.value),
                           output_token.length);
  std::string encode_output;
  base::Base64Encode(encode_input, &encode_output);
  *auth_token = scheme_ + " " + encode_output;
  return OK;
}


namespace {

// GSSAPI status codes consist of a calling error (essentially, a programmer
// bug), a routine error (defined by the RFC), and supplementary information,
// all bitwise-or'ed together in different regions of the 32 bit return value.
// This means a simple switch on the return codes is not sufficient.

int MapImportNameStatusToError(OM_uint32 major_status) {
  VLOG(1) << "import_name returned 0x" << std::hex << major_status;
  if (major_status == GSS_S_COMPLETE)
    return OK;
  if (GSS_CALLING_ERROR(major_status) != 0)
    return ERR_UNEXPECTED;
  OM_uint32 routine_error = GSS_ROUTINE_ERROR(major_status);
  switch (routine_error) {
    case GSS_S_FAILURE:
      // Looking at the MIT Kerberos implementation, this typically is returned
      // when memory allocation fails. However, the API does not guarantee
      // that this is the case, so using ERR_UNEXPECTED rather than
      // ERR_OUT_OF_MEMORY.
      return ERR_UNEXPECTED_SECURITY_LIBRARY_STATUS;
    case GSS_S_BAD_NAME:
    case GSS_S_BAD_NAMETYPE:
      return ERR_MALFORMED_IDENTITY;
    case GSS_S_DEFECTIVE_TOKEN:
      // Not mentioned in the API, but part of code.
      return ERR_UNEXPECTED_SECURITY_LIBRARY_STATUS;
    case GSS_S_BAD_MECH:
      return ERR_UNSUPPORTED_AUTH_SCHEME;
    default:
      return ERR_UNDOCUMENTED_SECURITY_LIBRARY_STATUS;
  }
}

int MapInitSecContextStatusToError(OM_uint32 major_status) {
  VLOG(1) << "init_sec_context returned 0x" << std::hex << major_status;
  // Although GSS_S_CONTINUE_NEEDED is an additional bit, it seems like
  // other code just checks if major_status is equivalent to it to indicate
  // that there are no other errors included.
  if (major_status == GSS_S_COMPLETE || major_status == GSS_S_CONTINUE_NEEDED)
    return OK;
  if (GSS_CALLING_ERROR(major_status) != 0)
    return ERR_UNEXPECTED;
  OM_uint32 routine_status = GSS_ROUTINE_ERROR(major_status);
  switch (routine_status) {
    case GSS_S_DEFECTIVE_TOKEN:
      return ERR_INVALID_RESPONSE;
    case GSS_S_DEFECTIVE_CREDENTIAL:
      // Not expected since this implementation uses the default credential.
      return ERR_UNEXPECTED_SECURITY_LIBRARY_STATUS;
    case GSS_S_BAD_SIG:
      // Probably won't happen, but it's a bad response.
      return ERR_INVALID_RESPONSE;
    case GSS_S_NO_CRED:
      return ERR_INVALID_AUTH_CREDENTIALS;
    case GSS_S_CREDENTIALS_EXPIRED:
      return ERR_INVALID_AUTH_CREDENTIALS;
    case GSS_S_BAD_BINDINGS:
      // This only happens with mutual authentication.
      return ERR_UNEXPECTED_SECURITY_LIBRARY_STATUS;
    case GSS_S_NO_CONTEXT:
      return ERR_UNEXPECTED_SECURITY_LIBRARY_STATUS;
    case GSS_S_BAD_NAMETYPE:
      return ERR_UNSUPPORTED_AUTH_SCHEME;
    case GSS_S_BAD_NAME:
      return ERR_UNSUPPORTED_AUTH_SCHEME;
    case GSS_S_BAD_MECH:
      return ERR_UNEXPECTED_SECURITY_LIBRARY_STATUS;
    case GSS_S_FAILURE:
      // This should be an "Unexpected Security Status" according to the
      // GSSAPI documentation, but it's typically used to indicate that
      // credentials are not correctly set up on a user machine, such
      // as a missing credential cache or hitting this after calling
      // kdestroy.
      // TODO(cbentzel): Use minor code for even better mapping?
      return ERR_MISSING_AUTH_CREDENTIALS;
    default:
      if (routine_status != 0)
        return ERR_UNDOCUMENTED_SECURITY_LIBRARY_STATUS;
      break;
  }
  OM_uint32 supplemental_status = GSS_SUPPLEMENTARY_INFO(major_status);
  // Replays could indicate an attack.
  if (supplemental_status & (GSS_S_DUPLICATE_TOKEN | GSS_S_OLD_TOKEN |
                             GSS_S_UNSEQ_TOKEN | GSS_S_GAP_TOKEN))
    return ERR_INVALID_RESPONSE;

  // At this point, every documented status has been checked.
  return ERR_UNDOCUMENTED_SECURITY_LIBRARY_STATUS;
}

}  // anonymous namespace

int HttpAuthGSSAPI::GetNextSecurityToken(const std::string& spn,
                                         const std::string& channel_bindings,
                                         gss_buffer_t in_token,
                                         gss_buffer_t out_token) {
  // Create a name for the principal
  // TODO(cbentzel): Just do this on the first pass?
  std::string spn_principal = spn;
  gss_buffer_desc spn_buffer = GSS_C_EMPTY_BUFFER;
  spn_buffer.value = const_cast<char*>(spn_principal.c_str());
  spn_buffer.length = spn_principal.size() + 1;
  OM_uint32 minor_status = 0;
  gss_name_t principal_name = GSS_C_NO_NAME;
  OM_uint32 major_status = library_->import_name(
      &minor_status,
      &spn_buffer,
      GSS_C_NT_HOSTBASED_SERVICE,
      &principal_name);
  int rv = MapImportNameStatusToError(major_status);
  if (rv != OK) {
    LOG(ERROR) << "Problem importing name from "
               << "spn \"" << spn_principal << "\"\n"
               << DisplayExtendedStatus(library_, major_status, minor_status);
    return rv;
  }
  ScopedName scoped_name(principal_name, library_);

  // Continue creating a security context.
  OM_uint32 req_flags = 0;
  if (can_delegate_)
    req_flags |= GSS_C_DELEG_FLAG;
  major_status = library_->init_sec_context(
      &minor_status, GSS_C_NO_CREDENTIAL, scoped_sec_context_.receive(),
      principal_name, gss_oid_, req_flags, GSS_C_INDEFINITE,
      GSS_C_NO_CHANNEL_BINDINGS, in_token,
      nullptr,  // actual_mech_type
      out_token,
      nullptr,  // ret flags
      nullptr);
  rv = MapInitSecContextStatusToError(major_status);
  if (rv != OK) {
    LOG(ERROR) << "Problem initializing context. \n"
               << DisplayExtendedStatus(library_, major_status, minor_status)
               << '\n'
               << DescribeContext(library_, scoped_sec_context_.get());
  }
  return rv;
}

}  // namespace net
