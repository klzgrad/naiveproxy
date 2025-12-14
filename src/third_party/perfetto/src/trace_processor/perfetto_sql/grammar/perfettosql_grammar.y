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
// Reprint of input file "buildtools/sqlite_src/src/parse.y".
// Symbols:
//   0 $                      162 INDEX                 
//   1 SEMI                   163 ALTER                 
//   2 EXPLAIN                164 ADD                   
//   3 QUERY                  165 WINDOW                
//   4 PLAN                   166 OVER                  
//   5 BEGIN                  167 FILTER                
//   6 TRANSACTION            168 COLUMN                
//   7 DEFERRED               169 AGG_FUNCTION          
//   8 IMMEDIATE              170 AGG_COLUMN            
//   9 EXCLUSIVE              171 TRUEFALSE             
//  10 COMMIT                 172 FUNCTION              
//  11 END                    173 UPLUS                 
//  12 ROLLBACK               174 UMINUS                
//  13 SAVEPOINT              175 TRUTH                 
//  14 RELEASE                176 REGISTER              
//  15 TO                     177 VECTOR                
//  16 TABLE                  178 SELECT_COLUMN         
//  17 CREATE                 179 IF_NULL_ROW           
//  18 IF                     180 ASTERISK              
//  19 NOT                    181 SPAN                  
//  20 EXISTS                 182 ERROR                 
//  21 TEMP                   183 QNUMBER               
//  22 LP                     184 SPACE                 
//  23 RP                     185 COMMENT               
//  24 AS                     186 ILLEGAL               
//  25 COMMA                  187 input                 
//  26 WITHOUT                188 cmdlist               
//  27 ABORT                  189 ecmd                  
//  28 ACTION                 190 cmdx                  
//  29 AFTER                  191 explain               
//  30 ANALYZE                192 cmd                   
//  31 ASC                    193 transtype             
//  32 ATTACH                 194 trans_opt             
//  33 BEFORE                 195 nm                    
//  34 BY                     196 savepoint_opt         
//  35 CASCADE                197 create_table          
//  36 CAST                   198 create_table_args     
//  37 CONFLICT               199 createkw              
//  38 DATABASE               200 temp                  
//  39 DESC                   201 ifnotexists           
//  40 DETACH                 202 dbnm                  
//  41 EACH                   203 columnlist            
//  42 FAIL                   204 conslist_opt          
//  43 OR                     205 table_option_set      
//  44 AND                    206 select                
//  45 IS                     207 table_option          
//  46 ISNOT                  208 columnname            
//  47 MATCH                  209 carglist              
//  48 LIKE_KW                210 typetoken             
//  49 BETWEEN                211 typename              
//  50 IN                     212 signed                
//  51 ISNULL                 213 plus_num              
//  52 NOTNULL                214 minus_num             
//  53 NE                     215 scanpt                
//  54 EQ                     216 scantok               
//  55 GT                     217 ccons                 
//  56 LE                     218 term                  
//  57 LT                     219 expr                  
//  58 GE                     220 onconf                
//  59 ESCAPE                 221 sortorder             
//  60 ID                     222 autoinc               
//  61 COLUMNKW               223 eidlist_opt           
//  62 DO                     224 refargs               
//  63 FOR                    225 defer_subclause       
//  64 IGNORE                 226 generated             
//  65 INITIALLY              227 refarg                
//  66 INSTEAD                228 refact                
//  67 NO                     229 init_deferred_pred_opt
//  68 KEY                    230 conslist              
//  69 OF                     231 tconscomma            
//  70 OFFSET                 232 tcons                 
//  71 PRAGMA                 233 sortlist              
//  72 RAISE                  234 eidlist               
//  73 RECURSIVE              235 defer_subclause_opt   
//  74 REPLACE                236 orconf                
//  75 RESTRICT               237 resolvetype           
//  76 ROW                    238 raisetype             
//  77 ROWS                   239 ifexists              
//  78 TRIGGER                240 fullname              
//  79 VACUUM                 241 selectnowith          
//  80 VIEW                   242 oneselect             
//  81 VIRTUAL                243 wqlist                
//  82 WITH                   244 multiselect_op        
//  83 NULLS                  245 distinct              
//  84 FIRST                  246 selcollist            
//  85 LAST                   247 from                  
//  86 CURRENT                248 where_opt             
//  87 FOLLOWING              249 groupby_opt           
//  88 PARTITION              250 having_opt            
//  89 PRECEDING              251 orderby_opt           
//  90 RANGE                  252 limit_opt             
//  91 UNBOUNDED              253 window_clause         
//  92 EXCLUDE                254 values                
//  93 GROUPS                 255 nexprlist             
//  94 OTHERS                 256 mvalues               
//  95 TIES                   257 sclp                  
//  96 GENERATED              258 as                    
//  97 ALWAYS                 259 seltablist            
//  98 MATERIALIZED           260 stl_prefix            
//  99 REINDEX                261 joinop                
// 100 RENAME                 262 on_using              
// 101 CTIME_KW               263 indexed_by            
// 102 ANY                    264 exprlist              
// 103 BITAND                 265 xfullname             
// 104 BITOR                  266 idlist                
// 105 LSHIFT                 267 indexed_opt           
// 106 RSHIFT                 268 nulls                 
// 107 PLUS                   269 with                  
// 108 MINUS                  270 where_opt_ret         
// 109 STAR                   271 setlist               
// 110 SLASH                  272 insert_cmd            
// 111 REM                    273 idlist_opt            
// 112 CONCAT                 274 upsert                
// 113 PTR                    275 returning             
// 114 COLLATE                276 filter_over           
// 115 BITNOT                 277 likeop                
// 116 ON                     278 between_op            
// 117 INDEXED                279 in_op                 
// 118 STRING                 280 paren_exprlist        
// 119 JOIN_KW                281 case_operand          
// 120 CONSTRAINT             282 case_exprlist         
// 121 DEFAULT                283 case_else             
// 122 NULL                   284 uniqueflag            
// 123 PRIMARY                285 collate               
// 124 UNIQUE                 286 vinto                 
// 125 CHECK                  287 nmnum                 
// 126 REFERENCES             288 trigger_decl          
// 127 AUTOINCR               289 trigger_cmd_list      
// 128 INSERT                 290 trigger_time          
// 129 DELETE                 291 trigger_event         
// 130 UPDATE                 292 foreach_clause        
// 131 SET                    293 when_clause           
// 132 DEFERRABLE             294 trigger_cmd           
// 133 FOREIGN                295 trnm                  
// 134 DROP                   296 tridxby               
// 135 UNION                  297 database_kw_opt       
// 136 ALL                    298 key_opt               
// 137 EXCEPT                 299 add_column_fullname   
// 138 INTERSECT              300 kwcolumn_opt          
// 139 SELECT                 301 create_vtab           
// 140 VALUES                 302 vtabarglist           
// 141 DISTINCT               303 vtabarg               
// 142 DOT                    304 vtabargtoken          
// 143 FROM                   305 lp                    
// 144 JOIN                   306 anylist               
// 145 USING                  307 wqitem                
// 146 ORDER                  308 wqas                  
// 147 GROUP                  309 withnm                
// 148 HAVING                 310 windowdefn_list       
// 149 LIMIT                  311 windowdefn            
// 150 WHERE                  312 window                
// 151 RETURNING              313 frame_opt             
// 152 INTO                   314 part_opt              
// 153 NOTHING                315 filter_clause         
// 154 FLOAT                  316 over_clause           
// 155 BLOB                   317 range_or_rows         
// 156 INTEGER                318 frame_bound           
// 157 VARIABLE               319 frame_bound_s         
// 158 CASE                   320 frame_bound_e         
// 159 WHEN                   321 frame_exclude_opt     
// 160 THEN                   322 frame_exclude         
// 161 ELSE                  
explain ::= EXPLAIN.
explain ::= EXPLAIN QUERY PLAN.
cmdx ::= cmd.
cmd ::= BEGIN transtype trans_opt.
transtype ::=.
transtype ::= DEFERRED.
transtype ::= IMMEDIATE.
transtype ::= EXCLUSIVE.
cmd ::= COMMIT|END trans_opt.
cmd ::= ROLLBACK trans_opt.
cmd ::= SAVEPOINT nm.
cmd ::= RELEASE savepoint_opt nm.
cmd ::= ROLLBACK trans_opt TO savepoint_opt nm.
create_table ::= createkw temp TABLE ifnotexists nm dbnm.
createkw ::= CREATE.
ifnotexists ::=.
ifnotexists ::= IF NOT EXISTS.
temp ::= TEMP.
temp ::=.
create_table_args ::= LP columnlist conslist_opt RP table_option_set.
create_table_args ::= AS select.
table_option_set ::=.
table_option_set ::= table_option_set COMMA table_option.
table_option ::= WITHOUT nm.
table_option ::= nm.
columnname ::= nm typetoken.
typetoken ::=.
typetoken ::= typename LP signed RP.
typetoken ::= typename LP signed COMMA signed RP.
typename ::= typename ID|STRING.
scanpt ::=.
scantok ::=.
ccons ::= CONSTRAINT nm.
ccons ::= DEFAULT scantok term.
ccons ::= DEFAULT LP expr RP.
ccons ::= DEFAULT PLUS scantok term.
ccons ::= DEFAULT MINUS scantok term.
ccons ::= DEFAULT scantok ID|INDEXED.
ccons ::= NOT NULL onconf.
ccons ::= PRIMARY KEY sortorder onconf autoinc.
ccons ::= UNIQUE onconf.
ccons ::= CHECK LP expr RP.
ccons ::= REFERENCES nm eidlist_opt refargs.
ccons ::= defer_subclause.
ccons ::= COLLATE ID|STRING.
generated ::= LP expr RP.
generated ::= LP expr RP ID.
autoinc ::=.
autoinc ::= AUTOINCR.
refargs ::=.
refargs ::= refargs refarg.
refarg ::= MATCH nm.
refarg ::= ON INSERT refact.
refarg ::= ON DELETE refact.
refarg ::= ON UPDATE refact.
refact ::= SET NULL.
refact ::= SET DEFAULT.
refact ::= CASCADE.
refact ::= RESTRICT.
refact ::= NO ACTION.
defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt.
defer_subclause ::= DEFERRABLE init_deferred_pred_opt.
init_deferred_pred_opt ::=.
init_deferred_pred_opt ::= INITIALLY DEFERRED.
init_deferred_pred_opt ::= INITIALLY IMMEDIATE.
conslist_opt ::=.
tconscomma ::= COMMA.
tcons ::= CONSTRAINT nm.
tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf.
tcons ::= UNIQUE LP sortlist RP onconf.
tcons ::= CHECK LP expr RP onconf.
tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt.
defer_subclause_opt ::=.
onconf ::=.
onconf ::= ON CONFLICT resolvetype.
orconf ::=.
orconf ::= OR resolvetype.
resolvetype ::= IGNORE.
resolvetype ::= REPLACE.
cmd ::= DROP TABLE ifexists fullname.
ifexists ::= IF EXISTS.
ifexists ::=.
cmd ::= createkw temp VIEW ifnotexists nm dbnm eidlist_opt AS select.
cmd ::= DROP VIEW ifexists fullname.
cmd ::= select.
select ::= WITH wqlist selectnowith.
select ::= WITH RECURSIVE wqlist selectnowith.
select ::= selectnowith.
selectnowith ::= selectnowith multiselect_op oneselect.
multiselect_op ::= UNION.
multiselect_op ::= UNION ALL.
multiselect_op ::= EXCEPT|INTERSECT.
oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt.
oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt window_clause orderby_opt limit_opt.
values ::= VALUES LP nexprlist RP.
oneselect ::= mvalues.
mvalues ::= values COMMA LP nexprlist RP.
mvalues ::= mvalues COMMA LP nexprlist RP.
distinct ::= DISTINCT.
distinct ::= ALL.
distinct ::=.
sclp ::=.
selcollist ::= sclp scanpt expr scanpt as.
selcollist ::= sclp scanpt STAR.
selcollist ::= sclp scanpt nm DOT STAR.
as ::= AS nm.
as ::=.
from ::=.
from ::= FROM seltablist.
stl_prefix ::= seltablist joinop.
stl_prefix ::=.
seltablist ::= stl_prefix nm dbnm as on_using.
seltablist ::= stl_prefix nm dbnm as indexed_by on_using.
seltablist ::= stl_prefix nm dbnm LP exprlist RP as on_using.
seltablist ::= stl_prefix LP select RP as on_using.
seltablist ::= stl_prefix LP seltablist RP as on_using.
dbnm ::=.
dbnm ::= DOT nm.
fullname ::= nm.
fullname ::= nm DOT nm.
xfullname ::= nm.
xfullname ::= nm DOT nm.
xfullname ::= nm DOT nm AS nm.
xfullname ::= nm AS nm.
joinop ::= COMMA|JOIN.
joinop ::= JOIN_KW JOIN.
joinop ::= JOIN_KW nm JOIN.
joinop ::= JOIN_KW nm nm JOIN.
on_using ::= ON expr.
on_using ::= USING LP idlist RP.
on_using ::=. [OR]
indexed_opt ::=.
indexed_by ::= INDEXED BY nm.
indexed_by ::= NOT INDEXED.
orderby_opt ::=.
orderby_opt ::= ORDER BY sortlist.
sortlist ::= sortlist COMMA expr sortorder nulls.
sortlist ::= expr sortorder nulls.
sortorder ::= ASC.
sortorder ::= DESC.
sortorder ::=.
nulls ::= NULLS FIRST.
nulls ::= NULLS LAST.
nulls ::=.
groupby_opt ::=.
groupby_opt ::= GROUP BY nexprlist.
having_opt ::=.
having_opt ::= HAVING expr.
limit_opt ::=.
limit_opt ::= LIMIT expr.
limit_opt ::= LIMIT expr OFFSET expr.
limit_opt ::= LIMIT expr COMMA expr.
cmd ::= with DELETE FROM xfullname indexed_opt where_opt_ret.
where_opt ::=.
where_opt ::= WHERE expr.
where_opt_ret ::=.
where_opt_ret ::= WHERE expr.
where_opt_ret ::= RETURNING selcollist.
where_opt_ret ::= WHERE expr RETURNING selcollist.
cmd ::= with UPDATE orconf xfullname indexed_opt SET setlist from where_opt_ret.
setlist ::= setlist COMMA nm EQ expr.
setlist ::= setlist COMMA LP idlist RP EQ expr.
setlist ::= nm EQ expr.
setlist ::= LP idlist RP EQ expr.
cmd ::= with insert_cmd INTO xfullname idlist_opt select upsert.
cmd ::= with insert_cmd INTO xfullname idlist_opt DEFAULT VALUES returning.
upsert ::=.
upsert ::= RETURNING selcollist.
upsert ::= ON CONFLICT LP sortlist RP where_opt DO UPDATE SET setlist where_opt upsert.
upsert ::= ON CONFLICT LP sortlist RP where_opt DO NOTHING upsert.
upsert ::= ON CONFLICT DO NOTHING returning.
upsert ::= ON CONFLICT DO UPDATE SET setlist where_opt returning.
returning ::= RETURNING selcollist.
insert_cmd ::= INSERT orconf.
insert_cmd ::= REPLACE.
idlist_opt ::=.
idlist_opt ::= LP idlist RP.
idlist ::= idlist COMMA nm.
idlist ::= nm.
expr ::= LP expr RP.
expr ::= ID|INDEXED|JOIN_KW.
expr ::= nm DOT nm.
expr ::= nm DOT nm DOT nm.
term ::= NULL|FLOAT|BLOB.
term ::= STRING.
term ::= INTEGER.
expr ::= VARIABLE.
expr ::= expr COLLATE ID|STRING.
expr ::= CAST LP expr AS typetoken RP.
expr ::= ID|INDEXED|JOIN_KW LP distinct exprlist RP.
expr ::= ID|INDEXED|JOIN_KW LP distinct exprlist ORDER BY sortlist RP.
expr ::= ID|INDEXED|JOIN_KW LP STAR RP.
expr ::= ID|INDEXED|JOIN_KW LP distinct exprlist RP filter_over.
expr ::= ID|INDEXED|JOIN_KW LP distinct exprlist ORDER BY sortlist RP filter_over.
expr ::= ID|INDEXED|JOIN_KW LP STAR RP filter_over.
term ::= CTIME_KW.
expr ::= LP nexprlist COMMA expr RP.
expr ::= expr AND expr.
expr ::= expr OR expr.
expr ::= expr LT|GT|GE|LE expr.
expr ::= expr EQ|NE expr.
expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr.
expr ::= expr PLUS|MINUS expr.
expr ::= expr STAR|SLASH|REM expr.
expr ::= expr CONCAT expr.
likeop ::= NOT LIKE_KW|MATCH.
expr ::= expr likeop expr. [LIKE_KW]
expr ::= expr likeop expr ESCAPE expr. [LIKE_KW]
expr ::= expr ISNULL|NOTNULL.
expr ::= expr NOT NULL.
expr ::= expr IS expr.
expr ::= expr IS NOT expr.
expr ::= expr IS NOT DISTINCT FROM expr.
expr ::= expr IS DISTINCT FROM expr.
expr ::= NOT expr.
expr ::= BITNOT expr.
expr ::= PLUS|MINUS expr. [BITNOT]
expr ::= expr PTR expr.
between_op ::= BETWEEN.
between_op ::= NOT BETWEEN.
expr ::= expr between_op expr AND expr. [BETWEEN]
in_op ::= IN.
in_op ::= NOT IN.
expr ::= expr in_op LP exprlist RP. [IN]
expr ::= LP select RP.
expr ::= expr in_op LP select RP. [IN]
expr ::= expr in_op nm dbnm paren_exprlist. [IN]
expr ::= EXISTS LP select RP.
expr ::= CASE case_operand case_exprlist case_else END.
case_exprlist ::= case_exprlist WHEN expr THEN expr.
case_exprlist ::= WHEN expr THEN expr.
case_else ::= ELSE expr.
case_else ::=.
case_operand ::=.
exprlist ::=.
nexprlist ::= nexprlist COMMA expr.
nexprlist ::= expr.
paren_exprlist ::=.
paren_exprlist ::= LP exprlist RP.
cmd ::= createkw uniqueflag INDEX ifnotexists nm dbnm ON nm LP sortlist RP where_opt.
uniqueflag ::= UNIQUE.
uniqueflag ::=.
eidlist_opt ::=.
eidlist_opt ::= LP eidlist RP.
eidlist ::= eidlist COMMA nm collate sortorder.
eidlist ::= nm collate sortorder.
collate ::=.
collate ::= COLLATE ID|STRING.
cmd ::= DROP INDEX ifexists fullname.
cmd ::= VACUUM vinto.
cmd ::= VACUUM nm vinto.
vinto ::= INTO expr.
vinto ::=.
cmd ::= PRAGMA nm dbnm.
cmd ::= PRAGMA nm dbnm EQ nmnum.
cmd ::= PRAGMA nm dbnm LP nmnum RP.
cmd ::= PRAGMA nm dbnm EQ minus_num.
cmd ::= PRAGMA nm dbnm LP minus_num RP.
plus_num ::= PLUS INTEGER|FLOAT.
minus_num ::= MINUS INTEGER|FLOAT.
cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END.
trigger_decl ::= temp TRIGGER ifnotexists nm dbnm trigger_time trigger_event ON fullname foreach_clause when_clause.
trigger_time ::= BEFORE|AFTER.
trigger_time ::= INSTEAD OF.
trigger_time ::=.
trigger_event ::= DELETE|INSERT.
trigger_event ::= UPDATE.
trigger_event ::= UPDATE OF idlist.
when_clause ::=.
when_clause ::= WHEN expr.
trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI.
trigger_cmd_list ::= trigger_cmd SEMI.
trnm ::= nm DOT nm.
tridxby ::= INDEXED BY nm.
tridxby ::= NOT INDEXED.
trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist from where_opt scanpt.
trigger_cmd ::= scanpt insert_cmd INTO trnm idlist_opt select upsert scanpt.
trigger_cmd ::= DELETE FROM trnm tridxby where_opt scanpt.
trigger_cmd ::= scanpt select scanpt.
expr ::= RAISE LP IGNORE RP.
expr ::= RAISE LP raisetype COMMA expr RP.
raisetype ::= ROLLBACK.
raisetype ::= ABORT.
raisetype ::= FAIL.
cmd ::= DROP TRIGGER ifexists fullname.
cmd ::= ATTACH database_kw_opt expr AS expr key_opt.
cmd ::= DETACH database_kw_opt expr.
key_opt ::=.
key_opt ::= KEY expr.
cmd ::= REINDEX.
cmd ::= REINDEX nm dbnm.
cmd ::= ANALYZE.
cmd ::= ANALYZE nm dbnm.
cmd ::= ALTER TABLE fullname RENAME TO nm.
cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist.
cmd ::= ALTER TABLE fullname DROP kwcolumn_opt nm.
add_column_fullname ::= fullname.
cmd ::= ALTER TABLE fullname RENAME kwcolumn_opt nm TO nm.
cmd ::= create_vtab.
cmd ::= create_vtab LP vtabarglist RP.
create_vtab ::= createkw VIRTUAL TABLE ifnotexists nm dbnm USING nm.
vtabarg ::=.
vtabargtoken ::= ANY.
vtabargtoken ::= lp anylist RP.
lp ::= LP.
with ::= WITH wqlist.
with ::= WITH RECURSIVE wqlist.
wqas ::= AS.
wqas ::= AS MATERIALIZED.
wqas ::= AS NOT MATERIALIZED.
wqitem ::= withnm eidlist_opt wqas LP select RP.
withnm ::= nm.
wqlist ::= wqitem.
wqlist ::= wqlist COMMA wqitem.
windowdefn_list ::= windowdefn_list COMMA windowdefn.
windowdefn ::= nm AS LP window RP.
window ::= PARTITION BY nexprlist orderby_opt frame_opt.
window ::= nm PARTITION BY nexprlist orderby_opt frame_opt.
window ::= ORDER BY sortlist frame_opt.
window ::= nm ORDER BY sortlist frame_opt.
window ::= nm frame_opt.
frame_opt ::=.
frame_opt ::= range_or_rows frame_bound_s frame_exclude_opt.
frame_opt ::= range_or_rows BETWEEN frame_bound_s AND frame_bound_e frame_exclude_opt.
range_or_rows ::= RANGE|ROWS|GROUPS.
frame_bound_s ::= frame_bound.
frame_bound_s ::= UNBOUNDED PRECEDING.
frame_bound_e ::= frame_bound.
frame_bound_e ::= UNBOUNDED FOLLOWING.
frame_bound ::= expr PRECEDING|FOLLOWING.
frame_bound ::= CURRENT ROW.
frame_exclude_opt ::=.
frame_exclude_opt ::= EXCLUDE frame_exclude.
frame_exclude ::= NO OTHERS.
frame_exclude ::= CURRENT ROW.
frame_exclude ::= GROUP|TIES.
window_clause ::= WINDOW windowdefn_list.
filter_over ::= filter_clause over_clause.
filter_over ::= over_clause.
filter_over ::= filter_clause.
over_clause ::= OVER LP window RP.
over_clause ::= OVER nm.
filter_clause ::= FILTER LP WHERE expr RP.
term ::= QNUMBER.
input ::= cmdlist.
cmdlist ::= cmdlist ecmd.
cmdlist ::= ecmd.
ecmd ::= SEMI.
ecmd ::= cmdx SEMI.
ecmd ::= explain cmdx SEMI.
trans_opt ::=.
trans_opt ::= TRANSACTION.
trans_opt ::= TRANSACTION nm.
savepoint_opt ::= SAVEPOINT.
savepoint_opt ::=.
cmd ::= create_table create_table_args.
table_option_set ::= table_option.
columnlist ::= columnlist COMMA columnname carglist.
columnlist ::= columnname carglist.
nm ::= ID|INDEXED|JOIN_KW.
nm ::= STRING.
typetoken ::= typename.
typename ::= ID|STRING.
signed ::= plus_num.
signed ::= minus_num.
carglist ::= carglist ccons.
carglist ::=.
ccons ::= NULL onconf.
ccons ::= GENERATED ALWAYS AS generated.
ccons ::= AS generated.
conslist_opt ::= COMMA conslist.
conslist ::= conslist tconscomma tcons.
conslist ::= tcons.
tconscomma ::=.
defer_subclause_opt ::= defer_subclause.
resolvetype ::= raisetype.
selectnowith ::= oneselect.
oneselect ::= values.
sclp ::= selcollist COMMA.
as ::= ID|STRING.
indexed_opt ::= indexed_by.
returning ::=.
expr ::= term.
likeop ::= LIKE_KW|MATCH.
case_operand ::= expr.
exprlist ::= nexprlist.
nmnum ::= plus_num.
nmnum ::= nm.
nmnum ::= ON.
nmnum ::= DELETE.
nmnum ::= DEFAULT.
plus_num ::= INTEGER|FLOAT.
foreach_clause ::=.
foreach_clause ::= FOR EACH ROW.
trnm ::= nm.
tridxby ::=.
database_kw_opt ::= DATABASE.
database_kw_opt ::=.
kwcolumn_opt ::=.
kwcolumn_opt ::= COLUMNKW.
vtabarglist ::= vtabarg.
vtabarglist ::= vtabarglist COMMA vtabarg.
vtabarg ::= vtabarg vtabargtoken.
anylist ::=.
anylist ::= anylist LP anylist RP.
anylist ::= anylist ANY.
with ::=.
windowdefn_list ::= windowdefn.
window ::= frame_opt.

%token SPACE COMMENT ILLEGAL.
