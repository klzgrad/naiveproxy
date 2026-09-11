/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_SYNTAQLITE_INLINE_DISPATCH_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_SYNTAQLITE_INLINE_DISPATCH_H_

/* Inline dialect dispatch for syntaqlite_perfetto.c — replaces the dialect
 * function-pointer indirection with direct calls. Enabled via
 * -DSYNTAQLITE_INLINE_DIALECT_DISPATCH=\"...\". */

#define SYNQ_PARSER_ALLOC(d, m, c) SynqPerfettoParseAlloc(m, c)
#define SYNQ_PARSER_INIT(d, p, c) SynqPerfettoParseInit(p, c)
#define SYNQ_PARSER_FINALIZE(d, p) SynqPerfettoParseFinalize(p)
#define SYNQ_PARSER_FREE(d, p, f) SynqPerfettoParseFree(p, f)
#define SYNQ_PARSER_FEED(d, p, t, m) SynqPerfettoParse(p, t, m)
/* SynqPerfettoParseTrace is only declared under !NDEBUG (Lemon trace hook). */
#ifndef NDEBUG
#define SYNQ_PARSER_TRACE(d, f, s) \
  do {                             \
    SynqPerfettoParseTrace(f, s);  \
  } while (0)
#else
#define SYNQ_PARSER_TRACE(d, f, s) ((void)(d), (void)(f), (void)(s))
#endif
#define SYNQ_GET_TOKEN(env, z, t) SynqPerfettoGetToken(env, z, t)

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_SYNTAQLITE_INLINE_DISPATCH_H_
