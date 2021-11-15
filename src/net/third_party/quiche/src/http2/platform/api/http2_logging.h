#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_LOGGING_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_LOGGING_H_

#include "common/platform/api/quiche_logging.h"

#define HTTP2_LOG(severity) QUICHE_LOG(severity)

#define HTTP2_VLOG(verbose_level) QUICHE_VLOG(verbose_level)

#define HTTP2_DLOG(severity) QUICHE_DLOG(severity)

#define HTTP2_DLOG_IF(severity, condition) QUICHE_DLOG_IF(severity, condition)

#define HTTP2_DVLOG(verbose_level) QUICHE_DVLOG(verbose_level)

#define HTTP2_DVLOG_IF(verbose_level, condition) \
  QUICHE_DVLOG_IF(verbose_level, condition)

#define HTTP2_LOG_FIRST_N(severity, n) QUICHE_LOG_FIRST_N(severity, n)

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_LOGGING_H_
