/*
 * Copyright (C) 2024 The Android Open Source Project
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

%name PreprocessorGrammarParse
%token_prefix PPTK_
%start_symbol input

%include {
#include "src/trace_processor/perfetto_sql/preprocessor/preprocessor_grammar_interface.h"

#define YYNOERRORRECOVERY 1
#define YYPARSEFREENEVERNULL 1
}

%token_type {struct PreprocessorGrammarToken}
%default_type {struct PreprocessorGrammarTokenBounds}

%extra_context {struct PreprocessorGrammarState* state}
%syntax_error {
  OnPreprocessorSyntaxError(state, &yyminor);
}

input ::= cmd.  {
  OnPreprocessorEnd(state);
}

cmd ::= sql SEMI.
cmd ::= apply SEMI.

apply ::= APPLY COMMA|AND(J) TRUE|FALSE(P) ID(X) LP applylist(Y) RP. {
  OnPreprocessorApply(state, &X, &J, &P, Y, 0);
}
apply ::= APPLY COMMA|AND(J) TRUE|FALSE(P) ID(X) LP applylist(Y) RP LP applylist(Z) RP. {
  OnPreprocessorApply(state, &X, &J, &P, Y, Z);
}

%type applylist {struct PreprocessorGrammarApplyList*}
%destructor applylist { OnPreprocessorFreeApplyList(state, $$); }
applylist(A) ::= applylist(F) COMMA tokenlist(X). {
  A = OnPreprocessorAppendApplyList(F, &X);
}
applylist(A) ::= tokenlist(F). {
  A = OnPreprocessorAppendApplyList(OnPreprocessorCreateApplyList(), &F);
}
applylist(A) ::=. {
  A = OnPreprocessorCreateApplyList();
}

sql ::= sql sqltoken.
sql ::= sqltoken.

sqltoken ::= COMMA.
sqltoken ::= commalesssqltoken.

commalesssqltoken(A) ::= OPAQUE(X). {
  A = (struct PreprocessorGrammarTokenBounds) {X, X};
}
commalesssqltoken(A) ::= minvocationid(S) EXCLAIM LP marglist RP(F). {
  A = (struct PreprocessorGrammarTokenBounds) {S, F};
  OnPreprocessorMacroEnd(state, &S, &F);
}
commalesssqltoken(A) ::= LP(S) sql RP(F).  {
  A = (struct PreprocessorGrammarTokenBounds) {S, F};
}
commalesssqltoken(A) ::= LP(S) RP(F). {
  A = (struct PreprocessorGrammarTokenBounds) {S, F};
}
commalesssqltoken(A) ::= ID(X). {
  A = (struct PreprocessorGrammarTokenBounds) {X, X};
}
commalesssqltoken(A) ::= VARIABLE(X). {
  A = (struct PreprocessorGrammarTokenBounds) {X, X};
  OnPreprocessorVariable(state, &X);
}

%type minvocationid {struct PreprocessorGrammarToken}
minvocationid(A) ::= ID(X). {
  A = X;
  OnPreprocessorMacroId(state, &X);
}

marglist ::= marglistinner.
marglist ::=.

marglistinner ::= marglistinner COMMA marg.
marglistinner ::= marg.

marg ::= tokenlist(X). {
  OnPreprocessorMacroArg(state, &X);
}

tokenlist(A) ::= tokenlist(S) commalesssqltoken(F). {
  A = (struct PreprocessorGrammarTokenBounds) {S.start, F.end};
}
tokenlist(A) ::= commalesssqltoken(X). {
  A = (struct PreprocessorGrammarTokenBounds) {X.start, X.end};
}
