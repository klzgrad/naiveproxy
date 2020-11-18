// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_LOGGING_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_LOGGING_H_

#include "net/third_party/quiche/src/common/platform/api/quiche_logging.h"

// Please note following QUIC_LOG are platform dependent:
// INFO severity can be degraded (to VLOG(1) or DVLOG(1)).
// Some platforms may not support QUIC_LOG_FIRST_N or QUIC_LOG_EVERY_N_SEC, and
// they would simply be translated to LOG.

#define QUIC_DVLOG QUICHE_DVLOG
#define QUIC_DVLOG_IF QUICHE_DVLOG_IF
#define QUIC_DLOG QUICHE_DLOG
#define QUIC_DLOG_IF QUICHE_DLOG_IF
#define QUIC_VLOG QUICHE_VLOG
#define QUIC_LOG QUICHE_LOG
#define QUIC_LOG_FIRST_N QUICHE_LOG_FIRST_N
#define QUIC_LOG_EVERY_N_SEC QUICHE_LOG_EVERY_N_SEC
#define QUIC_LOG_IF QUICHE_LOG_IF

#define QUIC_PREDICT_FALSE QUICHE_PREDICT_FALSE
#define QUIC_PREDICT_TRUE QUICHE_PREDICT_TRUE

// This is a noop in release build.
#define QUIC_NOTREACHED QUICHE_NOTREACHED

#define QUIC_PLOG QUICHE_PLOG

#define QUIC_DLOG_INFO_IS_ON QUICHE_DLOG_INFO_IS_ON
#define QUIC_LOG_INFO_IS_ON QUICHE_LOG_INFO_IS_ON
#define QUIC_LOG_WARNING_IS_ON QUICHE_LOG_WARNING_IS_ON
#define QUIC_LOG_ERROR_IS_ON QUICHE_LOG_ERROR_IS_ON

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_LOGGING_H_
