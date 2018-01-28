// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/view_cache_helper.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/strings/stringprintf.h"
#include "net/base/escape.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_cache.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/url_request/url_request_context.h"

#define VIEW_CACHE_HEAD                                 \
  "<html><meta charset=\"utf-8\">"                      \
  "<meta http-equiv=\"Content-Security-Policy\" "       \
  "  content=\"object-src 'none'; script-src 'none'\">" \
  "<body><table>"

#define VIEW_CACHE_TAIL \
  "</table></body></html>"

namespace net {

namespace {

std::string FormatEntryInfo(disk_cache::Entry* entry,
                            const std::string& url_prefix) {
  std::string key = entry->GetKey();
  GURL url = GURL(url_prefix + key);
  std::string row =
      "<tr><td><a href=\"" + url.spec() + "\">" + EscapeForHTML(key) +
      "</a></td></tr>";
  return row;
}

}  // namespace.

ViewCacheHelper::ViewCacheHelper()
    : context_(NULL),
      disk_cache_(NULL),
      entry_(NULL),
      buf_len_(0),
      index_(0),
      data_(NULL),
      next_state_(STATE_NONE),
      weak_factory_(this) {
}

ViewCacheHelper::~ViewCacheHelper() {
  if (entry_)
    entry_->Close();
}

int ViewCacheHelper::GetEntryInfoHTML(const std::string& key,
                                      const URLRequestContext* context,
                                      std::string* out,
                                      const CompletionCallback& callback) {
  return GetInfoHTML(key, context, std::string(), out, callback);
}

int ViewCacheHelper::GetContentsHTML(const URLRequestContext* context,
                                     const std::string& url_prefix,
                                     std::string* out,
                                     const CompletionCallback& callback) {
  return GetInfoHTML(std::string(), context, url_prefix, out, callback);
}

// static
void ViewCacheHelper::HexDump(const char *buf, size_t buf_len,
                              std::string* result) {
  const size_t kMaxRows = 16;
  int offset = 0;

  const unsigned char *p;
  while (buf_len) {
    base::StringAppendF(result, "%08x: ", offset);
    offset += kMaxRows;

    p = (const unsigned char *) buf;

    size_t i;
    size_t row_max = std::min(kMaxRows, buf_len);

    // print hex codes:
    for (i = 0; i < row_max; ++i)
      base::StringAppendF(result, "%02x ", *p++);
    for (i = row_max; i < kMaxRows; ++i)
      result->append("   ");
    result->append(" ");

    // print ASCII glyphs if possible:
    p = (const unsigned char *) buf;
    for (i = 0; i < row_max; ++i, ++p) {
      if (*p < 0x7F && *p > 0x1F) {
        AppendEscapedCharForHTML(*p, result);
      } else {
        result->push_back('.');
      }
    }

    result->push_back('\n');

    buf += row_max;
    buf_len -= row_max;
  }
}

//-----------------------------------------------------------------------------

int ViewCacheHelper::GetInfoHTML(const std::string& key,
                                 const URLRequestContext* context,
                                 const std::string& url_prefix,
                                 std::string* out,
                                 const CompletionCallback& callback) {
  DCHECK(callback_.is_null());
  DCHECK(context);
  key_ = key;
  context_ = context;
  url_prefix_ = url_prefix;
  data_ = out;
  next_state_ = STATE_GET_BACKEND;
  int rv = DoLoop(OK);

  if (rv == ERR_IO_PENDING)
    callback_ = callback;

  return rv;
}

void ViewCacheHelper::DoCallback(int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  DCHECK(!callback_.is_null());

  callback_.Run(rv);
  callback_.Reset();
}

void ViewCacheHelper::HandleResult(int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  DCHECK_NE(ERR_FAILED, rv);
  context_ = NULL;
  if (!callback_.is_null())
    DoCallback(rv);
}

int ViewCacheHelper::DoLoop(int result) {
  DCHECK(next_state_ != STATE_NONE);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_GET_BACKEND:
        DCHECK_EQ(OK, rv);
        rv = DoGetBackend();
        break;
      case STATE_GET_BACKEND_COMPLETE:
        rv = DoGetBackendComplete(rv);
        break;
      case STATE_OPEN_NEXT_ENTRY:
        DCHECK_EQ(OK, rv);
        rv = DoOpenNextEntry();
        break;
      case STATE_OPEN_NEXT_ENTRY_COMPLETE:
        rv = DoOpenNextEntryComplete(rv);
        break;
      case STATE_OPEN_ENTRY:
        DCHECK_EQ(OK, rv);
        rv = DoOpenEntry();
        break;
      case STATE_OPEN_ENTRY_COMPLETE:
        rv = DoOpenEntryComplete(rv);
        break;
      case STATE_READ_RESPONSE:
        DCHECK_EQ(OK, rv);
        rv = DoReadResponse();
        break;
      case STATE_READ_RESPONSE_COMPLETE:
        rv = DoReadResponseComplete(rv);
        break;
      case STATE_READ_DATA:
        DCHECK_EQ(OK, rv);
        rv = DoReadData();
        break;
      case STATE_READ_DATA_COMPLETE:
        rv = DoReadDataComplete(rv);
        break;

      default:
        NOTREACHED() << "bad state";
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);

  if (rv != ERR_IO_PENDING)
    HandleResult(rv);

  return rv;
}

int ViewCacheHelper::DoGetBackend() {
  next_state_ = STATE_GET_BACKEND_COMPLETE;

  if (!context_->http_transaction_factory())
    return ERR_FAILED;

  HttpCache* http_cache = context_->http_transaction_factory()->GetCache();
  if (!http_cache)
    return ERR_FAILED;

  return http_cache->GetBackend(
      &disk_cache_, base::Bind(&ViewCacheHelper::OnIOComplete,
                               base::Unretained(this)));
}

int ViewCacheHelper::DoGetBackendComplete(int result) {
  if (result == ERR_FAILED) {
    data_->append("no disk cache");
    return OK;
  }

  DCHECK_EQ(OK, result);
  if (key_.empty()) {
    data_->assign(VIEW_CACHE_HEAD);
    DCHECK(!iter_);
    next_state_ = STATE_OPEN_NEXT_ENTRY;
    return OK;
  }

  next_state_ = STATE_OPEN_ENTRY;
  return OK;
}

int ViewCacheHelper::DoOpenNextEntry() {
  next_state_ = STATE_OPEN_NEXT_ENTRY_COMPLETE;
  if (!iter_)
    iter_ = disk_cache_->CreateIterator();
  return
      iter_->OpenNextEntry(&entry_, base::Bind(&ViewCacheHelper::OnIOComplete,
                                               base::Unretained(this)));
}

int ViewCacheHelper::DoOpenNextEntryComplete(int result) {
  if (result == ERR_FAILED) {
    data_->append(VIEW_CACHE_TAIL);
    return OK;
  }

  DCHECK_EQ(OK, result);
  data_->append(FormatEntryInfo(entry_, url_prefix_));
  entry_->Close();
  entry_ = NULL;

  next_state_ = STATE_OPEN_NEXT_ENTRY;
  return OK;
}

int ViewCacheHelper::DoOpenEntry() {
  next_state_ = STATE_OPEN_ENTRY_COMPLETE;
  return disk_cache_->OpenEntry(
      key_, &entry_,
      base::Bind(&ViewCacheHelper::OnIOComplete, base::Unretained(this)));
}

int ViewCacheHelper::DoOpenEntryComplete(int result) {
  if (result == ERR_FAILED) {
    data_->append("no matching cache entry for: " + EscapeForHTML(key_));
    return OK;
  }

  data_->assign(VIEW_CACHE_HEAD);
  data_->append(EscapeForHTML(entry_->GetKey()));
  next_state_ = STATE_READ_RESPONSE;
  return OK;
}

int ViewCacheHelper::DoReadResponse() {
  next_state_ = STATE_READ_RESPONSE_COMPLETE;
  buf_len_ = entry_->GetDataSize(0);
  if (!buf_len_)
    return buf_len_;

  buf_ = new IOBuffer(buf_len_);
  return entry_->ReadData(
      0,
      0,
      buf_.get(),
      buf_len_,
      base::Bind(&ViewCacheHelper::OnIOComplete, weak_factory_.GetWeakPtr()));
}

int ViewCacheHelper::DoReadResponseComplete(int result) {
  if (result && result == buf_len_) {
    HttpResponseInfo response;
    bool truncated;
    if (HttpCache::ParseResponseInfo(
            buf_->data(), buf_len_, &response, &truncated) &&
        response.headers.get()) {
      if (truncated)
        data_->append("<pre>RESPONSE_INFO_TRUNCATED</pre>");

      data_->append("<hr><pre>");
      data_->append(EscapeForHTML(response.headers->GetStatusLine()));
      data_->push_back('\n');

      size_t iter = 0;
      std::string name, value;
      while (response.headers->EnumerateHeaderLines(&iter, &name, &value)) {
        data_->append(EscapeForHTML(name));
        data_->append(": ");
        data_->append(EscapeForHTML(value));
        data_->push_back('\n');
      }
      data_->append("</pre>");
    }
  }

  index_ = 0;
  next_state_ = STATE_READ_DATA;
  return OK;
}

int ViewCacheHelper::DoReadData() {
  data_->append("<hr><pre>");

  next_state_ = STATE_READ_DATA_COMPLETE;
  buf_len_ = entry_->GetDataSize(index_);
  if (!buf_len_)
    return buf_len_;

  buf_ = new IOBuffer(buf_len_);
  return entry_->ReadData(
      index_,
      0,
      buf_.get(),
      buf_len_,
      base::Bind(&ViewCacheHelper::OnIOComplete, weak_factory_.GetWeakPtr()));
}

int ViewCacheHelper::DoReadDataComplete(int result) {
  if (result && result == buf_len_) {
    HexDump(buf_->data(), buf_len_, data_);
  }
  data_->append("</pre>");
  index_++;
  if (index_ < HttpCache::kNumCacheEntryDataIndices) {
    next_state_ = STATE_READ_DATA;
  } else {
    data_->append(VIEW_CACHE_TAIL);
    entry_->Close();
    entry_ = NULL;
  }
  return OK;
}

void ViewCacheHelper::OnIOComplete(int result) {
  DoLoop(result);
}

}  // namespace net.
