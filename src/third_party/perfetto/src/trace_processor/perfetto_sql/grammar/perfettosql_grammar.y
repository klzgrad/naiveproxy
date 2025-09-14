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

%token CREATE REPLACE PERFETTO MACRO INCLUDE MODULE RETURNS FUNCTION.

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
//   0 $                      160 ELSE                  
//   1 SEMI                   161 INDEX                 
//   2 EXPLAIN                162 ALTER                 
//   3 QUERY                  163 ADD                   
//   4 PLAN                   164 WINDOW                
//   5 BEGIN                  165 OVER                  
//   6 TRANSACTION            166 FILTER                
//   7 DEFERRED               167 COLUMN                
//   8 IMMEDIATE              168 AGG_FUNCTION          
//   9 EXCLUSIVE              169 AGG_COLUMN            
//  10 COMMIT                 170 TRUEFALSE             
//  11 END                    171 ISNOT                 
//  12 ROLLBACK               172 FUNCTION              
//  13 SAVEPOINT              173 UMINUS                
//  14 RELEASE                174 UPLUS                 
//  15 TO                     175 TRUTH                 
//  16 TABLE                  176 REGISTER              
//  17 CREATE                 177 VECTOR                
//  18 IF                     178 SELECT_COLUMN         
//  19 NOT                    179 IF_NULL_ROW           
//  20 EXISTS                 180 ASTERISK              
//  21 TEMP                   181 SPAN                  
//  22 LP                     182 ERROR                 
//  23 RP                     183 SPACE                 
//  24 AS                     184 ILLEGAL               
//  25 COMMA                  185 input                 
//  26 WITHOUT                186 cmdlist               
//  27 ABORT                  187 ecmd                  
//  28 ACTION                 188 cmdx                  
//  29 AFTER                  189 explain               
//  30 ANALYZE                190 cmd                   
//  31 ASC                    191 transtype             
//  32 ATTACH                 192 trans_opt             
//  33 BEFORE                 193 nm                    
//  34 BY                     194 savepoint_opt         
//  35 CASCADE                195 create_table          
//  36 CAST                   196 create_table_args     
//  37 CONFLICT               197 createkw              
//  38 DATABASE               198 temp                  
//  39 DESC                   199 ifnotexists           
//  40 DETACH                 200 dbnm                  
//  41 EACH                   201 columnlist            
//  42 FAIL                   202 conslist_opt          
//  43 OR                     203 table_option_set      
//  44 AND                    204 select                
//  45 IS                     205 table_option          
//  46 MATCH                  206 columnname            
//  47 LIKE_KW                207 carglist              
//  48 BETWEEN                208 typetoken             
//  49 IN                     209 typename              
//  50 ISNULL                 210 signed                
//  51 NOTNULL                211 plus_num              
//  52 NE                     212 minus_num             
//  53 EQ                     213 scanpt                
//  54 GT                     214 scantok               
//  55 LE                     215 ccons                 
//  56 LT                     216 term                  
//  57 GE                     217 expr                  
//  58 ESCAPE                 218 onconf                
//  59 ID                     219 sortorder             
//  60 COLUMNKW               220 autoinc               
//  61 DO                     221 eidlist_opt           
//  62 FOR                    222 refargs               
//  63 IGNORE                 223 defer_subclause       
//  64 INITIALLY              224 generated             
//  65 INSTEAD                225 refarg                
//  66 NO                     226 refact                
//  67 KEY                    227 init_deferred_pred_opt
//  68 OF                     228 conslist              
//  69 OFFSET                 229 tconscomma            
//  70 PRAGMA                 230 tcons                 
//  71 RAISE                  231 sortlist              
//  72 RECURSIVE              232 eidlist               
//  73 REPLACE                233 defer_subclause_opt   
//  74 RESTRICT               234 orconf                
//  75 ROW                    235 resolvetype           
//  76 ROWS                   236 raisetype             
//  77 TRIGGER                237 ifexists              
//  78 VACUUM                 238 fullname              
//  79 VIEW                   239 selectnowith          
//  80 VIRTUAL                240 oneselect             
//  81 WITH                   241 wqlist                
//  82 NULLS                  242 multiselect_op        
//  83 FIRST                  243 distinct              
//  84 LAST                   244 selcollist            
//  85 CURRENT                245 from                  
//  86 FOLLOWING              246 where_opt             
//  87 PARTITION              247 groupby_opt           
//  88 PRECEDING              248 having_opt            
//  89 RANGE                  249 orderby_opt           
//  90 UNBOUNDED              250 limit_opt             
//  91 EXCLUDE                251 window_clause         
//  92 GROUPS                 252 values                
//  93 OTHERS                 253 nexprlist             
//  94 TIES                   254 sclp                  
//  95 GENERATED              255 as                    
//  96 ALWAYS                 256 seltablist            
//  97 MATERIALIZED           257 stl_prefix            
//  98 REINDEX                258 joinop                
//  99 RENAME                 259 on_using              
// 100 CTIME_KW               260 indexed_by            
// 101 ANY                    261 exprlist              
// 102 BITAND                 262 xfullname             
// 103 BITOR                  263 idlist                
// 104 LSHIFT                 264 indexed_opt           
// 105 RSHIFT                 265 nulls                 
// 106 PLUS                   266 with                  
// 107 MINUS                  267 where_opt_ret         
// 108 STAR                   268 setlist               
// 109 SLASH                  269 insert_cmd            
// 110 REM                    270 idlist_opt            
// 111 CONCAT                 271 upsert                
// 112 PTR                    272 returning             
// 113 COLLATE                273 filter_over           
// 114 BITNOT                 274 likeop                
// 115 ON                     275 between_op            
// 116 INDEXED                276 in_op                 
// 117 STRING                 277 paren_exprlist        
// 118 JOIN_KW                278 case_operand          
// 119 CONSTRAINT             279 case_exprlist         
// 120 DEFAULT                280 case_else             
// 121 NULL                   281 uniqueflag            
// 122 PRIMARY                282 collate               
// 123 UNIQUE                 283 vinto                 
// 124 CHECK                  284 nmnum                 
// 125 REFERENCES             285 trigger_decl          
// 126 AUTOINCR               286 trigger_cmd_list      
// 127 INSERT                 287 trigger_time          
// 128 DELETE                 288 trigger_event         
// 129 UPDATE                 289 foreach_clause        
// 130 SET                    290 when_clause           
// 131 DEFERRABLE             291 trigger_cmd           
// 132 FOREIGN                292 trnm                  
// 133 DROP                   293 tridxby               
// 134 UNION                  294 database_kw_opt       
// 135 ALL                    295 key_opt               
// 136 EXCEPT                 296 add_column_fullname   
// 137 INTERSECT              297 kwcolumn_opt          
// 138 SELECT                 298 create_vtab           
// 139 VALUES                 299 vtabarglist           
// 140 DISTINCT               300 vtabarg               
// 141 DOT                    301 vtabargtoken          
// 142 FROM                   302 lp                    
// 143 JOIN                   303 anylist               
// 144 USING                  304 wqitem                
// 145 ORDER                  305 wqas                  
// 146 GROUP                  306 windowdefn_list       
// 147 HAVING                 307 windowdefn            
// 148 LIMIT                  308 window                
// 149 WHERE                  309 frame_opt             
// 150 RETURNING              310 part_opt              
// 151 INTO                   311 filter_clause         
// 152 NOTHING                312 over_clause           
// 153 FLOAT                  313 range_or_rows         
// 154 BLOB                   314 frame_bound           
// 155 INTEGER                315 frame_bound_s         
// 156 VARIABLE               316 frame_bound_e         
// 157 CASE                   317 frame_exclude_opt     
// 158 WHEN                   318 frame_exclude         
// 159 THEN                  
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
values ::= values COMMA LP nexprlist RP.
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
expr ::= RAISE LP raisetype COMMA nm RP.
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
wqitem ::= nm eidlist_opt wqas LP select RP.
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

%token SPACE ILLEGAL.
