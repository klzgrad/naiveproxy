#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_LOGGING_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_LOGGING_H_

#include "net/http2/platform/impl/http2_logging_impl.h"

#define HTTP2_LOG(severity) HTTP2_LOG_IMPL(severity)

#define HTTP2_VLOG(verbose_level) HTTP2_VLOG_IMPL(verbose_level)

#define HTTP2_DLOG(severity) HTTP2_DLOG_IMPL(severity)

#define HTTP2_DLOG_IF(severity, condition) \
  HTTP2_DLOG_IF_IMPL(severity, condition)

#define HTTP2_DVLOG(verbose_level) HTTP2_DVLOG_IMPL(verbose_level)

#define HTTP2_DVLOG_IF(verbose_level, condition) \
  HTTP2_DVLOG_IF_IMPL(verbose_level, condition)

#define HTTP2_DLOG_EVERY_N(severity, n) HTTP2_DLOG_EVERY_N_IMPL(severity, n)

#define HTTP2_LOG_FIRST_N(severity, n) HTTP2_LOG_FIRST_N_IMPL(severity, n)

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_LOGGING_H_
