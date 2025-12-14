/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

%name PerfettoSqlParse
%token_prefix TK_
%start_symbol input

%include {
#include <stdio.h>
#include <stddef.h>
#include "src/trace_processor/perfetto_sql/grammar/perfettosql_grammar_interface.h"

#define YYNOERRORRECOVERY 1
#define YYPARSEFREENEVERNULL 1
}

%token CREATE REPLACE PERFETTO MACRO INCLUDE MODULE RETURNS FUNCTION DELEGATES.

%left OR.
%left AND.
%right NOT.
%left IS MATCH LIKE_KW BETWEEN IN ISNULL NOTNULL NE EQ.
%left GT LE LT GE.
%right ESCAPE.
%left BITAND BITOR LSHIFT RSHIFT.
%left PLUS MINUS.
%left STAR SLASH REM.
%left CONCAT PTR.
%left COLLATE.
%right BITNOT.
%nonassoc ON.

%fallback
// Taken from SQLite
  ID
  ABORT ACTION AFTER ANALYZE ASC ATTACH BEFORE BEGIN BY CASCADE CAST COLUMNKW
  CONFLICT DATABASE DEFERRED DESC DETACH DO
  EACH END EXCLUSIVE EXPLAIN FAIL FOR
  IGNORE IMMEDIATE INITIALLY INSTEAD LIKE_KW MATCH NO PLAN
  QUERY KEY OF OFFSET PRAGMA RAISE RECURSIVE RELEASE REPLACE RESTRICT ROW ROWS
  ROLLBACK SAVEPOINT TEMP TRIGGER VACUUM VIEW VIRTUAL WITH WITHOUT
  NULLS FIRST LAST
  CURRENT FOLLOWING PARTITION PRECEDING RANGE UNBOUNDED
  EXCLUDE GROUPS OTHERS TIES
  GENERATED ALWAYS
  MATERIALIZED
  REINDEX RENAME CTIME_KW IF
// Our additions.
  FUNCTION MODULE PERFETTO
  .
%wildcard ANY.

%token_type {struct PerfettoSqlToken}

%extra_context {struct PerfettoSqlParserState* state}
%syntax_error {
  OnPerfettoSqlSyntaxError(state, &yyminor);
}

// Helper function like scantok but usable by us.
pscantok(A) ::= . {
  assert( yyLookahead!=YYNOCODE );
  A = yyLookaheadToken;
}

// Shared rules
%type sql_argument_list { struct PerfettoSqlArgumentList* }
%destructor sql_argument_list { OnPerfettoSqlFreeArgumentList(state, $$); }
sql_argument_list(A) ::=. { A = 0; }
sql_argument_list(A) ::= sql_argument_list_nonempty(X). { A = X; }

sql_argument_type(A) ::= ID(B). { A = B; }
sql_argument_type(A) ::= ID(B) LP ID DOT ID RP. { A = B; }

%type sql_argument_list_nonempty { struct PerfettoSqlArgumentList* }
%destructor sql_argument_list_nonempty { OnPerfettoSqlFreeArgumentList(state, $$); }
sql_argument_list_nonempty(A) ::= sql_argument_list_nonempty(B) COMMA ID(C) sql_argument_type(D). {
  A = OnPerfettoSqlCreateOrAppendArgument(state, B, &C, &D);
}
sql_argument_list_nonempty(A) ::= ID(B) sql_argument_type(C). {
  A = OnPerfettoSqlCreateOrAppendArgument(state, 0, &B, &C);
}

%type table_schema { struct PerfettoSqlArgumentList* }
%destructor table_schema { OnPerfettoSqlFreeArgumentList(state, $$); }
table_schema(A) ::=. { A = 0; }
table_schema(A) ::= LP sql_argument_list_nonempty(B) RP. { A = B; }

// CREATE statements
%type or_replace {int}
or_replace(A) ::=.                    { A = 0; }
or_replace(A) ::= OR REPLACE.         { A = 1; }

// CREATE PERFETTO FUNCTION
cmd ::= CREATE or_replace(R) PERFETTO FUNCTION ID(N) LP sql_argument_list(A) RP RETURNS return_type(T) AS select(E) pscantok(S). {
  OnPerfettoSqlCreateFunction(state, R, &N, A, T, &E, &S);
}

// CREATE PERFETTO FUNCTION with delegating implementation
cmd ::= CREATE or_replace(R) PERFETTO FUNCTION ID(N) LP sql_argument_list(A) RP RETURNS return_type(T) DELEGATES TO ID(I) pscantok(S). {
  OnPerfettoSqlCreateDelegatingFunction(state, R, &N, A, T, &I, &S);
}

%type return_type { struct PerfettoSqlFnReturnType* }
%destructor return_type { OnPerfettoSqlFnFreeReturnType(state, $$); }
return_type(Y) ::= ID(X). {
  Y = OnPerfettoSqlCreateScalarReturnType(&X);
}
return_type(Y) ::= TABLE LP sql_argument_list_nonempty(A) RP. {
  Y = OnPerfettoSqlCreateTableReturnType(A);
}

table_impl(Y) ::=. {
  Y = (struct PerfettoSqlToken) {0, 0};
}
table_impl(Y) ::= USING ID(N). {
  Y = N;
}

// CREATE PERFETTO TABLE
cmd ::= CREATE or_replace(R) PERFETTO TABLE ID(N) table_impl(Y) table_schema(S) AS select(A) pscantok(Q). {
  OnPerfettoSqlCreateTable(state, R, &N, &Y, S, &A, &Q);
}

// CREATE PERFETTO VIEW
cmd ::= CREATE(C) or_replace(R) PERFETTO VIEW ID(N) table_schema(S) AS select(A) pscantok(Q). {
  OnPerfettoSqlCreateView(state, R, &C, &N, S, &A, &Q);
}

// CREATE PERFETTO INDEX
cmd ::= CREATE(C) or_replace(R) PERFETTO INDEX ID(N) ON ID(T) LP indexed_column_list(L) RP. {
  OnPerfettoSqlCreateIndex(state, R, &C, &N, &T, L);
}

%type indexed_column_list { struct PerfettoSqlIndexedColumnList* }
%destructor indexed_column_list { OnPerfettoSqlFreeIndexedColumnList(state, $$); }
indexed_column_list(A) ::= indexed_column_list(B) COMMA ID(C). {
  A = OnPerfettoSqlCreateOrAppendIndexedColumn(B, &C);
}
indexed_column_list(A) ::= ID(B). {
  A = OnPerfettoSqlCreateOrAppendIndexedColumn(0, &B);
}

// CREATE PERFETTO MACRO
cmd ::= CREATE or_replace(R) PERFETTO MACRO ID(N) LP macro_argument_list(A) RP RETURNS ID(T) AS macro_body(S) pscantok(B). {
  OnPerfettoSqlCreateMacro(state, R, &N, A, &T, &S, &B);
}
macro_body ::= ANY.
macro_body ::= macro_body ANY.

%type macro_argument_list_nonempty { struct PerfettoSqlMacroArgumentList* }
%destructor macro_argument_list_nonempty { OnPerfettoSqlFreeMacroArgumentList(state, $$); }
macro_argument_list_nonempty(A) ::= macro_argument_list_nonempty(D) COMMA ID(B) ID(C). {
  A = OnPerfettoSqlCreateOrAppendMacroArgument(state, D, &B, &C);
}
macro_argument_list_nonempty(A) ::= ID(B) ID(C). {
  A = OnPerfettoSqlCreateOrAppendMacroArgument(state, 0, &B, &C);
}

%type macro_argument_list { struct PerfettoSqlMacroArgumentList* }
%destructor macro_argument_list { OnPerfettoSqlFreeMacroArgumentList(state, $$); }
macro_argument_list(A) ::=. { A = 0; }
macro_argument_list(A) ::= macro_argument_list_nonempty(B). { A = B; }

// INCLUDE statement
cmd ::= INCLUDE PERFETTO MODULE module_name(M). {
  OnPerfettoSqlInclude(state, &M);
}
module_name(A) ::= ID|STAR|INTERSECT(B). {
  A = B;
}
module_name(A) ::= module_name(B) DOT ID|STAR|INTERSECT(C). {
  A = (struct PerfettoSqlToken) {B.ptr, C.ptr + C.n - B.ptr};
}

// DROP statement
cmd ::= DROP PERFETTO INDEX ID(N) ON ID(T). {
  OnPerfettoSqlDropIndex(state, &N, &T);
}
