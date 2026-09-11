--
-- Copyright 2026 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

CREATE PERFETTO MACRO __unparenthesize_identity(x Expr)
RETURNS Expr
AS $x;

-- Removes the surrounding parentheses from a parenthesised expression list,
-- yielding a bare comma-separated expression list. Typically used when you
-- already have something like `(a, b, c)` but need to splice it into a context
-- (e.g. a `GROUP BY` clause or a `PARTITION BY` clause) that expects an
-- unparenthesised list.
CREATE PERFETTO MACRO metasql_unparenthesize_exprlist(
  -- Parenthesised expression list to unparenthesise.
  expr ExprList
)
RETURNS UnparenExprList
AS __intrinsic_token_apply!(__unparenthesize_identity, $expr);
