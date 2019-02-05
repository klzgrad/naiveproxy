

/* this ALWAYS GENERATED file contains the proxy stub code */


 /* File created by MIDL compiler version 8.xx.xxxx */
/* at a redacted point in time
 */
/* Compiler settings for ../../third_party/iaccessible2/ia2_api_all.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.xx.xxxx 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if defined(_M_AMD64)


#pragma warning( disable: 4049 )  /* more than 64k source lines */
#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/
#pragma warning( disable: 4152 )  /* function/data pointer conversion in expression */

#define USE_STUBLESS_PROXY


/* verify that the <rpcproxy.h> version is high enough to compile this file*/
#ifndef __REDQ_RPCPROXY_H_VERSION__
#define __REQUIRED_RPCPROXY_H_VERSION__ 475
#endif


#include "rpcproxy.h"
#ifndef __RPCPROXY_H_VERSION__
#error this stub requires an updated version of <rpcproxy.h>
#endif /* __RPCPROXY_H_VERSION__ */


#include "ia2_api_all.h"

#define TYPE_FORMAT_STRING_SIZE   1467                              
#define PROC_FORMAT_STRING_SIZE   5445                              
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   3            

typedef struct _ia2_api_all_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } ia2_api_all_MIDL_TYPE_FORMAT_STRING;

typedef struct _ia2_api_all_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } ia2_api_all_MIDL_PROC_FORMAT_STRING;

typedef struct _ia2_api_all_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } ia2_api_all_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax = 
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};


extern const ia2_api_all_MIDL_TYPE_FORMAT_STRING ia2_api_all__MIDL_TypeFormatString;
extern const ia2_api_all_MIDL_PROC_FORMAT_STRING ia2_api_all__MIDL_ProcFormatString;
extern const ia2_api_all_MIDL_EXPR_FORMAT_STRING ia2_api_all__MIDL_ExprFormatString;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleRelation_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleRelation_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleAction_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleAction_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessible2_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessible2_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessible2_2_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessible2_2_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleComponent_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleComponent_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleValue_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleValue_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleText_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleText_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleText2_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleText2_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleEditableText_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleEditableText_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleHyperlink_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleHyperlink_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleHypertext_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleHypertext_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleHypertext2_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleHypertext2_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleTable_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleTable_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleTable2_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleTable2_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleTableCell_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleTableCell_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleImage_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleImage_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleApplication_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleApplication_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IAccessibleDocument_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IAccessibleDocument_ProxyInfo;


extern const USER_MARSHAL_ROUTINE_QUADRUPLE UserMarshalRoutines[ WIRE_MARSHAL_TABLE_SIZE ];

#if !defined(__RPC_WIN64__)
#error  Invalid build platform for this stub.
#endif

static const ia2_api_all_MIDL_PROC_FORMAT_STRING ia2_api_all__MIDL_ProcFormatString =
    {
        0,
        {

	/* Procedure get_appName */


	/* Procedure get_description */


	/* Procedure get_relationType */

			0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/*  2 */	NdrFcLong( 0x0 ),	/* 0 */
/*  6 */	NdrFcShort( 0x3 ),	/* 3 */
/*  8 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 10 */	NdrFcShort( 0x0 ),	/* 0 */
/* 12 */	NdrFcShort( 0x8 ),	/* 8 */
/* 14 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 16 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 18 */	NdrFcShort( 0x1 ),	/* 1 */
/* 20 */	NdrFcShort( 0x0 ),	/* 0 */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */
/* 24 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter name */


	/* Parameter description */


	/* Parameter relationType */

/* 26 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 28 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 30 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */


	/* Return value */


	/* Return value */

/* 32 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 34 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 36 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_appVersion */


	/* Procedure get_localizedRelationType */

/* 38 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 40 */	NdrFcLong( 0x0 ),	/* 0 */
/* 44 */	NdrFcShort( 0x4 ),	/* 4 */
/* 46 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 48 */	NdrFcShort( 0x0 ),	/* 0 */
/* 50 */	NdrFcShort( 0x8 ),	/* 8 */
/* 52 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 54 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 56 */	NdrFcShort( 0x1 ),	/* 1 */
/* 58 */	NdrFcShort( 0x0 ),	/* 0 */
/* 60 */	NdrFcShort( 0x0 ),	/* 0 */
/* 62 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter version */


	/* Parameter localizedRelationType */

/* 64 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 66 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 68 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */


	/* Return value */

/* 70 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 72 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 74 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnIndex */


	/* Procedure get_caretOffset */


	/* Procedure get_background */


	/* Procedure get_nTargets */

/* 76 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 78 */	NdrFcLong( 0x0 ),	/* 0 */
/* 82 */	NdrFcShort( 0x5 ),	/* 5 */
/* 84 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 86 */	NdrFcShort( 0x0 ),	/* 0 */
/* 88 */	NdrFcShort( 0x24 ),	/* 36 */
/* 90 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 92 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 94 */	NdrFcShort( 0x0 ),	/* 0 */
/* 96 */	NdrFcShort( 0x0 ),	/* 0 */
/* 98 */	NdrFcShort( 0x0 ),	/* 0 */
/* 100 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter columnIndex */


	/* Parameter offset */


	/* Parameter background */


	/* Parameter nTargets */

/* 102 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 104 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 106 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */


	/* Return value */


	/* Return value */

/* 108 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 110 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 112 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_target */

/* 114 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 116 */	NdrFcLong( 0x0 ),	/* 0 */
/* 120 */	NdrFcShort( 0x6 ),	/* 6 */
/* 122 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 124 */	NdrFcShort( 0x8 ),	/* 8 */
/* 126 */	NdrFcShort( 0x8 ),	/* 8 */
/* 128 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 130 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 132 */	NdrFcShort( 0x0 ),	/* 0 */
/* 134 */	NdrFcShort( 0x0 ),	/* 0 */
/* 136 */	NdrFcShort( 0x0 ),	/* 0 */
/* 138 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter targetIndex */

/* 140 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 142 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 144 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter target */

/* 146 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 148 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 150 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 152 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 154 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 156 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_targets */

/* 158 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 160 */	NdrFcLong( 0x0 ),	/* 0 */
/* 164 */	NdrFcShort( 0x7 ),	/* 7 */
/* 166 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 168 */	NdrFcShort( 0x8 ),	/* 8 */
/* 170 */	NdrFcShort( 0x24 ),	/* 36 */
/* 172 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 174 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 176 */	NdrFcShort( 0x1 ),	/* 1 */
/* 178 */	NdrFcShort( 0x0 ),	/* 0 */
/* 180 */	NdrFcShort( 0x0 ),	/* 0 */
/* 182 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maxTargets */

/* 184 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 186 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 188 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter targets */

/* 190 */	NdrFcShort( 0x113 ),	/* Flags:  must size, must free, out, simple ref, */
/* 192 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 194 */	NdrFcShort( 0x48 ),	/* Type Offset=72 */

	/* Parameter nTargets */

/* 196 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 198 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 200 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 202 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 204 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 206 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnExtent */


	/* Procedure nActions */

/* 208 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 210 */	NdrFcLong( 0x0 ),	/* 0 */
/* 214 */	NdrFcShort( 0x3 ),	/* 3 */
/* 216 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 218 */	NdrFcShort( 0x0 ),	/* 0 */
/* 220 */	NdrFcShort( 0x24 ),	/* 36 */
/* 222 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 224 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 226 */	NdrFcShort( 0x0 ),	/* 0 */
/* 228 */	NdrFcShort( 0x0 ),	/* 0 */
/* 230 */	NdrFcShort( 0x0 ),	/* 0 */
/* 232 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter nColumnsSpanned */


	/* Parameter nActions */

/* 234 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 236 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 238 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 240 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 242 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 244 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure doAction */

/* 246 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 248 */	NdrFcLong( 0x0 ),	/* 0 */
/* 252 */	NdrFcShort( 0x4 ),	/* 4 */
/* 254 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 256 */	NdrFcShort( 0x8 ),	/* 8 */
/* 258 */	NdrFcShort( 0x8 ),	/* 8 */
/* 260 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 262 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 264 */	NdrFcShort( 0x0 ),	/* 0 */
/* 266 */	NdrFcShort( 0x0 ),	/* 0 */
/* 268 */	NdrFcShort( 0x0 ),	/* 0 */
/* 270 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter actionIndex */

/* 272 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 274 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 276 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 278 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 280 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 282 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnDescription */


	/* Procedure get_description */

/* 284 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 286 */	NdrFcLong( 0x0 ),	/* 0 */
/* 290 */	NdrFcShort( 0x5 ),	/* 5 */
/* 292 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 294 */	NdrFcShort( 0x8 ),	/* 8 */
/* 296 */	NdrFcShort( 0x8 ),	/* 8 */
/* 298 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 300 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 302 */	NdrFcShort( 0x1 ),	/* 1 */
/* 304 */	NdrFcShort( 0x0 ),	/* 0 */
/* 306 */	NdrFcShort( 0x0 ),	/* 0 */
/* 308 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */


	/* Parameter actionIndex */

/* 310 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 312 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 314 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter description */


	/* Parameter description */

/* 316 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 318 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 320 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */


	/* Return value */

/* 322 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 324 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 326 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_keyBinding */

/* 328 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 330 */	NdrFcLong( 0x0 ),	/* 0 */
/* 334 */	NdrFcShort( 0x6 ),	/* 6 */
/* 336 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 338 */	NdrFcShort( 0x10 ),	/* 16 */
/* 340 */	NdrFcShort( 0x24 ),	/* 36 */
/* 342 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x5,		/* 5 */
/* 344 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 346 */	NdrFcShort( 0x1 ),	/* 1 */
/* 348 */	NdrFcShort( 0x0 ),	/* 0 */
/* 350 */	NdrFcShort( 0x0 ),	/* 0 */
/* 352 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter actionIndex */

/* 354 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 356 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 358 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter nMaxBindings */

/* 360 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 362 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 364 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter keyBindings */

/* 366 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 368 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 370 */	NdrFcShort( 0x5e ),	/* Type Offset=94 */

	/* Parameter nBindings */

/* 372 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 374 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 376 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 378 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 380 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 382 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_name */

/* 384 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 386 */	NdrFcLong( 0x0 ),	/* 0 */
/* 390 */	NdrFcShort( 0x7 ),	/* 7 */
/* 392 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 394 */	NdrFcShort( 0x8 ),	/* 8 */
/* 396 */	NdrFcShort( 0x8 ),	/* 8 */
/* 398 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 400 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 402 */	NdrFcShort( 0x1 ),	/* 1 */
/* 404 */	NdrFcShort( 0x0 ),	/* 0 */
/* 406 */	NdrFcShort( 0x0 ),	/* 0 */
/* 408 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter actionIndex */

/* 410 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 412 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 414 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter name */

/* 416 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 418 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 420 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 422 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 424 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 426 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_localizedName */

/* 428 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 430 */	NdrFcLong( 0x0 ),	/* 0 */
/* 434 */	NdrFcShort( 0x8 ),	/* 8 */
/* 436 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 438 */	NdrFcShort( 0x8 ),	/* 8 */
/* 440 */	NdrFcShort( 0x8 ),	/* 8 */
/* 442 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 444 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 446 */	NdrFcShort( 0x1 ),	/* 1 */
/* 448 */	NdrFcShort( 0x0 ),	/* 0 */
/* 450 */	NdrFcShort( 0x0 ),	/* 0 */
/* 452 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter actionIndex */

/* 454 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 456 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 458 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter localizedName */

/* 460 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 462 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 464 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 466 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 468 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 470 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nRelations */

/* 472 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 474 */	NdrFcLong( 0x0 ),	/* 0 */
/* 478 */	NdrFcShort( 0x1c ),	/* 28 */
/* 480 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 482 */	NdrFcShort( 0x0 ),	/* 0 */
/* 484 */	NdrFcShort( 0x24 ),	/* 36 */
/* 486 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 488 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 490 */	NdrFcShort( 0x0 ),	/* 0 */
/* 492 */	NdrFcShort( 0x0 ),	/* 0 */
/* 494 */	NdrFcShort( 0x0 ),	/* 0 */
/* 496 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter nRelations */

/* 498 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 500 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 502 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 504 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 506 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 508 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_relation */

/* 510 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 512 */	NdrFcLong( 0x0 ),	/* 0 */
/* 516 */	NdrFcShort( 0x1d ),	/* 29 */
/* 518 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 520 */	NdrFcShort( 0x8 ),	/* 8 */
/* 522 */	NdrFcShort( 0x8 ),	/* 8 */
/* 524 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 526 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 528 */	NdrFcShort( 0x0 ),	/* 0 */
/* 530 */	NdrFcShort( 0x0 ),	/* 0 */
/* 532 */	NdrFcShort( 0x0 ),	/* 0 */
/* 534 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter relationIndex */

/* 536 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 538 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 540 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter relation */

/* 542 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 544 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 546 */	NdrFcShort( 0x7c ),	/* Type Offset=124 */

	/* Return value */

/* 548 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 550 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 552 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_relations */

/* 554 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 556 */	NdrFcLong( 0x0 ),	/* 0 */
/* 560 */	NdrFcShort( 0x1e ),	/* 30 */
/* 562 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 564 */	NdrFcShort( 0x8 ),	/* 8 */
/* 566 */	NdrFcShort( 0x24 ),	/* 36 */
/* 568 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 570 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 572 */	NdrFcShort( 0x1 ),	/* 1 */
/* 574 */	NdrFcShort( 0x0 ),	/* 0 */
/* 576 */	NdrFcShort( 0x0 ),	/* 0 */
/* 578 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maxRelations */

/* 580 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 582 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 584 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter relations */

/* 586 */	NdrFcShort( 0x113 ),	/* Flags:  must size, must free, out, simple ref, */
/* 588 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 590 */	NdrFcShort( 0x96 ),	/* Type Offset=150 */

	/* Parameter nRelations */

/* 592 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 594 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 596 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 598 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 600 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 602 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure role */

/* 604 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 606 */	NdrFcLong( 0x0 ),	/* 0 */
/* 610 */	NdrFcShort( 0x1f ),	/* 31 */
/* 612 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 614 */	NdrFcShort( 0x0 ),	/* 0 */
/* 616 */	NdrFcShort( 0x24 ),	/* 36 */
/* 618 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 620 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 622 */	NdrFcShort( 0x0 ),	/* 0 */
/* 624 */	NdrFcShort( 0x0 ),	/* 0 */
/* 626 */	NdrFcShort( 0x0 ),	/* 0 */
/* 628 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter role */

/* 630 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 632 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 634 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 636 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 638 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 640 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure scrollTo */

/* 642 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 644 */	NdrFcLong( 0x0 ),	/* 0 */
/* 648 */	NdrFcShort( 0x20 ),	/* 32 */
/* 650 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 652 */	NdrFcShort( 0x6 ),	/* 6 */
/* 654 */	NdrFcShort( 0x8 ),	/* 8 */
/* 656 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 658 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 660 */	NdrFcShort( 0x0 ),	/* 0 */
/* 662 */	NdrFcShort( 0x0 ),	/* 0 */
/* 664 */	NdrFcShort( 0x0 ),	/* 0 */
/* 666 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter scrollType */

/* 668 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 670 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 672 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Return value */

/* 674 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 676 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 678 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure scrollToPoint */

/* 680 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 682 */	NdrFcLong( 0x0 ),	/* 0 */
/* 686 */	NdrFcShort( 0x21 ),	/* 33 */
/* 688 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 690 */	NdrFcShort( 0x16 ),	/* 22 */
/* 692 */	NdrFcShort( 0x8 ),	/* 8 */
/* 694 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 696 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 698 */	NdrFcShort( 0x0 ),	/* 0 */
/* 700 */	NdrFcShort( 0x0 ),	/* 0 */
/* 702 */	NdrFcShort( 0x0 ),	/* 0 */
/* 704 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter coordinateType */

/* 706 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 708 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 710 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter x */

/* 712 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 714 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 716 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter y */

/* 718 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 720 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 722 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 724 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 726 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 728 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_groupPosition */

/* 730 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 732 */	NdrFcLong( 0x0 ),	/* 0 */
/* 736 */	NdrFcShort( 0x22 ),	/* 34 */
/* 738 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 740 */	NdrFcShort( 0x0 ),	/* 0 */
/* 742 */	NdrFcShort( 0x5c ),	/* 92 */
/* 744 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 746 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 748 */	NdrFcShort( 0x0 ),	/* 0 */
/* 750 */	NdrFcShort( 0x0 ),	/* 0 */
/* 752 */	NdrFcShort( 0x0 ),	/* 0 */
/* 754 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter groupLevel */

/* 756 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 758 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 760 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter similarItemsInGroup */

/* 762 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 764 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 766 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter positionInGroup */

/* 768 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 770 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 772 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 774 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 776 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 778 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_states */

/* 780 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 782 */	NdrFcLong( 0x0 ),	/* 0 */
/* 786 */	NdrFcShort( 0x23 ),	/* 35 */
/* 788 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 790 */	NdrFcShort( 0x0 ),	/* 0 */
/* 792 */	NdrFcShort( 0x24 ),	/* 36 */
/* 794 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 796 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 798 */	NdrFcShort( 0x0 ),	/* 0 */
/* 800 */	NdrFcShort( 0x0 ),	/* 0 */
/* 802 */	NdrFcShort( 0x0 ),	/* 0 */
/* 804 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter states */

/* 806 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 808 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 810 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 812 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 814 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 816 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_extendedRole */

/* 818 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 820 */	NdrFcLong( 0x0 ),	/* 0 */
/* 824 */	NdrFcShort( 0x24 ),	/* 36 */
/* 826 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 828 */	NdrFcShort( 0x0 ),	/* 0 */
/* 830 */	NdrFcShort( 0x8 ),	/* 8 */
/* 832 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 834 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 836 */	NdrFcShort( 0x1 ),	/* 1 */
/* 838 */	NdrFcShort( 0x0 ),	/* 0 */
/* 840 */	NdrFcShort( 0x0 ),	/* 0 */
/* 842 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter extendedRole */

/* 844 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 846 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 848 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 850 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 852 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 854 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_localizedExtendedRole */

/* 856 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 858 */	NdrFcLong( 0x0 ),	/* 0 */
/* 862 */	NdrFcShort( 0x25 ),	/* 37 */
/* 864 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 866 */	NdrFcShort( 0x0 ),	/* 0 */
/* 868 */	NdrFcShort( 0x8 ),	/* 8 */
/* 870 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 872 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 874 */	NdrFcShort( 0x1 ),	/* 1 */
/* 876 */	NdrFcShort( 0x0 ),	/* 0 */
/* 878 */	NdrFcShort( 0x0 ),	/* 0 */
/* 880 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter localizedExtendedRole */

/* 882 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 884 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 886 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 888 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 890 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 892 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nExtendedStates */

/* 894 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 896 */	NdrFcLong( 0x0 ),	/* 0 */
/* 900 */	NdrFcShort( 0x26 ),	/* 38 */
/* 902 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 904 */	NdrFcShort( 0x0 ),	/* 0 */
/* 906 */	NdrFcShort( 0x24 ),	/* 36 */
/* 908 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 910 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 912 */	NdrFcShort( 0x0 ),	/* 0 */
/* 914 */	NdrFcShort( 0x0 ),	/* 0 */
/* 916 */	NdrFcShort( 0x0 ),	/* 0 */
/* 918 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter nExtendedStates */

/* 920 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 922 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 924 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 926 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 928 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 930 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_extendedStates */

/* 932 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 934 */	NdrFcLong( 0x0 ),	/* 0 */
/* 938 */	NdrFcShort( 0x27 ),	/* 39 */
/* 940 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 942 */	NdrFcShort( 0x8 ),	/* 8 */
/* 944 */	NdrFcShort( 0x24 ),	/* 36 */
/* 946 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 948 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 950 */	NdrFcShort( 0x1 ),	/* 1 */
/* 952 */	NdrFcShort( 0x0 ),	/* 0 */
/* 954 */	NdrFcShort( 0x0 ),	/* 0 */
/* 956 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maxExtendedStates */

/* 958 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 960 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 962 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter extendedStates */

/* 964 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 966 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 968 */	NdrFcShort( 0xac ),	/* Type Offset=172 */

	/* Parameter nExtendedStates */

/* 970 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 972 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 974 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 976 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 978 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 980 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_localizedExtendedStates */

/* 982 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 984 */	NdrFcLong( 0x0 ),	/* 0 */
/* 988 */	NdrFcShort( 0x28 ),	/* 40 */
/* 990 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 992 */	NdrFcShort( 0x8 ),	/* 8 */
/* 994 */	NdrFcShort( 0x24 ),	/* 36 */
/* 996 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 998 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1000 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1002 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1004 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1006 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maxLocalizedExtendedStates */

/* 1008 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1010 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1012 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter localizedExtendedStates */

/* 1014 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 1016 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1018 */	NdrFcShort( 0xac ),	/* Type Offset=172 */

	/* Parameter nLocalizedExtendedStates */

/* 1020 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1022 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1024 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1026 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1028 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1030 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_uniqueID */

/* 1032 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1034 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1038 */	NdrFcShort( 0x29 ),	/* 41 */
/* 1040 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1042 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1044 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1046 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1048 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1050 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1052 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1054 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1056 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter uniqueID */

/* 1058 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1060 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1062 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1064 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1066 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1068 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_windowHandle */

/* 1070 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1072 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1076 */	NdrFcShort( 0x2a ),	/* 42 */
/* 1078 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1080 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1082 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1084 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1086 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1088 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1090 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1092 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1094 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter windowHandle */

/* 1096 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1098 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1100 */	NdrFcShort( 0xe6 ),	/* Type Offset=230 */

	/* Return value */

/* 1102 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1104 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1106 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_indexInParent */

/* 1108 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1110 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1114 */	NdrFcShort( 0x2b ),	/* 43 */
/* 1116 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1118 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1120 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1122 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1124 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1126 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1128 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1130 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1132 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter indexInParent */

/* 1134 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1136 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1138 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1140 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1142 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1144 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_locale */

/* 1146 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1148 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1152 */	NdrFcShort( 0x2c ),	/* 44 */
/* 1154 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1156 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1158 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1160 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1162 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1164 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1166 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1168 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1170 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter locale */

/* 1172 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 1174 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1176 */	NdrFcShort( 0xf4 ),	/* Type Offset=244 */

	/* Return value */

/* 1178 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1180 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1182 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_attributes */

/* 1184 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1186 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1190 */	NdrFcShort( 0x2d ),	/* 45 */
/* 1192 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1194 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1196 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1198 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1200 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1202 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1204 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1206 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1208 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter attributes */

/* 1210 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1212 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1214 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 1216 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1218 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1220 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_attribute */

/* 1222 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1224 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1228 */	NdrFcShort( 0x2e ),	/* 46 */
/* 1230 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1232 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1234 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1236 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 1238 */	0xa,		/* 10 */
			0x7,		/* Ext Flags:  new corr desc, clt corr check, srv corr check, */
/* 1240 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1242 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1244 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1246 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter name */

/* 1248 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 1250 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1252 */	NdrFcShort( 0x10e ),	/* Type Offset=270 */

	/* Parameter attribute */

/* 1254 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 1256 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1258 */	NdrFcShort( 0x4bc ),	/* Type Offset=1212 */

	/* Return value */

/* 1260 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1262 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1264 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_accessibleWithCaret */

/* 1266 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1268 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1272 */	NdrFcShort( 0x2f ),	/* 47 */
/* 1274 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1276 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1278 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1280 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 1282 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1284 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1286 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1288 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1290 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter accessible */

/* 1292 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 1294 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1296 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter caretOffset */

/* 1298 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1300 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1302 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1304 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1306 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1308 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_relationTargetsOfType */

/* 1310 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1312 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1316 */	NdrFcShort( 0x30 ),	/* 48 */
/* 1318 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 1320 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1322 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1324 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x5,		/* 5 */
/* 1326 */	0xa,		/* 10 */
			0x7,		/* Ext Flags:  new corr desc, clt corr check, srv corr check, */
/* 1328 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1330 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1332 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1334 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter type */

/* 1336 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 1338 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1340 */	NdrFcShort( 0x10e ),	/* Type Offset=270 */

	/* Parameter maxTargets */

/* 1342 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1344 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1346 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter targets */

/* 1348 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 1350 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1352 */	NdrFcShort( 0x4c6 ),	/* Type Offset=1222 */

	/* Parameter nTargets */

/* 1354 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1356 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1358 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1360 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1362 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 1364 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_locationInParent */

/* 1366 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1368 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1372 */	NdrFcShort( 0x3 ),	/* 3 */
/* 1374 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1376 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1378 */	NdrFcShort( 0x40 ),	/* 64 */
/* 1380 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 1382 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1384 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1386 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1388 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1390 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter x */

/* 1392 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1394 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1396 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter y */

/* 1398 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1400 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1402 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1404 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1406 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1408 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_foreground */

/* 1410 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1412 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1416 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1418 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1420 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1422 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1424 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1426 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1428 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1430 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1432 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1434 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter foreground */

/* 1436 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1438 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1440 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1442 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1444 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1446 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_currentValue */

/* 1448 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1450 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1454 */	NdrFcShort( 0x3 ),	/* 3 */
/* 1456 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1458 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1460 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1462 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1464 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1466 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1468 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1470 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1472 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter currentValue */

/* 1474 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 1476 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1478 */	NdrFcShort( 0x4bc ),	/* Type Offset=1212 */

	/* Return value */

/* 1480 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1482 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1484 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure setCurrentValue */

/* 1486 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1488 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1492 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1494 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1496 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1498 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1500 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 1502 */	0xa,		/* 10 */
			0x85,		/* Ext Flags:  new corr desc, srv corr check, has big byval param */
/* 1504 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1506 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1508 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1510 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter value */

/* 1512 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 1514 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1516 */	NdrFcShort( 0x4ec ),	/* Type Offset=1260 */

	/* Return value */

/* 1518 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1520 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1522 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_maximumValue */

/* 1524 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1526 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1530 */	NdrFcShort( 0x5 ),	/* 5 */
/* 1532 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1534 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1536 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1538 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1540 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1542 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1544 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1546 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1548 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maximumValue */

/* 1550 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 1552 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1554 */	NdrFcShort( 0x4bc ),	/* Type Offset=1212 */

	/* Return value */

/* 1556 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1558 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1560 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_minimumValue */

/* 1562 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1564 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1568 */	NdrFcShort( 0x6 ),	/* 6 */
/* 1570 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1572 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1574 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1576 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1578 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1580 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1582 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1584 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1586 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter minimumValue */

/* 1588 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 1590 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1592 */	NdrFcShort( 0x4bc ),	/* Type Offset=1212 */

	/* Return value */

/* 1594 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1596 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1598 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure copyText */


	/* Procedure addSelection */

/* 1600 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1602 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1606 */	NdrFcShort( 0x3 ),	/* 3 */
/* 1608 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1610 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1612 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1614 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 1616 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1618 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1620 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1622 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1624 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startOffset */


	/* Parameter startOffset */

/* 1626 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1628 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1630 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */


	/* Parameter endOffset */

/* 1632 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1634 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1636 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 1638 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1640 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1642 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_attributes */

/* 1644 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1646 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1650 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1652 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 1654 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1656 */	NdrFcShort( 0x40 ),	/* 64 */
/* 1658 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x5,		/* 5 */
/* 1660 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1662 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1664 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1666 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1668 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 1670 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1672 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1674 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter startOffset */

/* 1676 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1678 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1680 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 1682 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1684 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1686 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter textAttributes */

/* 1688 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1690 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1692 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 1694 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1696 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 1698 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_characterExtents */

/* 1700 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1702 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1706 */	NdrFcShort( 0x6 ),	/* 6 */
/* 1708 */	NdrFcShort( 0x40 ),	/* X64 Stack size/offset = 64 */
/* 1710 */	NdrFcShort( 0xe ),	/* 14 */
/* 1712 */	NdrFcShort( 0x78 ),	/* 120 */
/* 1714 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x7,		/* 7 */
/* 1716 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1718 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1720 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1722 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1724 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 1726 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1728 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1730 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter coordType */

/* 1732 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1734 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1736 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter x */

/* 1738 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1740 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1742 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter y */

/* 1744 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1746 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1748 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter width */

/* 1750 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1752 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 1754 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter height */

/* 1756 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1758 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 1760 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1762 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1764 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 1766 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nRows */


	/* Procedure get_nSelections */

/* 1768 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1770 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1774 */	NdrFcShort( 0x7 ),	/* 7 */
/* 1776 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1778 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1780 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1782 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1784 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1786 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1788 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1790 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1792 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter rowCount */


	/* Parameter nSelections */

/* 1794 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1796 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1798 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 1800 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1802 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1804 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_offsetAtPoint */

/* 1806 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1808 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1812 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1814 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 1816 */	NdrFcShort( 0x16 ),	/* 22 */
/* 1818 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1820 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x5,		/* 5 */
/* 1822 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1824 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1826 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1828 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1830 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter x */

/* 1832 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1834 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1836 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter y */

/* 1838 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1840 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1842 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter coordType */

/* 1844 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1846 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1848 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter offset */

/* 1850 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1852 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1854 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1856 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1858 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 1860 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_selection */

/* 1862 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1864 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1868 */	NdrFcShort( 0x9 ),	/* 9 */
/* 1870 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 1872 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1874 */	NdrFcShort( 0x40 ),	/* 64 */
/* 1876 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 1878 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1880 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1882 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1884 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1886 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter selectionIndex */

/* 1888 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1890 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1892 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter startOffset */

/* 1894 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1896 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1898 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 1900 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1902 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1904 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1906 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1908 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1910 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_text */

/* 1912 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1914 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1918 */	NdrFcShort( 0xa ),	/* 10 */
/* 1920 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 1922 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1924 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1926 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 1928 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1930 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1932 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1934 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1936 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startOffset */

/* 1938 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1940 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1942 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 1944 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1946 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1948 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter text */

/* 1950 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1952 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1954 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 1956 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1958 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1960 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_textBeforeOffset */

/* 1962 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1964 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1968 */	NdrFcShort( 0xb ),	/* 11 */
/* 1970 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 1972 */	NdrFcShort( 0xe ),	/* 14 */
/* 1974 */	NdrFcShort( 0x40 ),	/* 64 */
/* 1976 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x6,		/* 6 */
/* 1978 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1980 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1982 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1984 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1986 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 1988 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1990 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1992 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter boundaryType */

/* 1994 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1996 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1998 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter startOffset */

/* 2000 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2002 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2004 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2006 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2008 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2010 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter text */

/* 2012 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 2014 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2016 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 2018 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2020 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 2022 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_textAfterOffset */

/* 2024 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2026 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2030 */	NdrFcShort( 0xc ),	/* 12 */
/* 2032 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 2034 */	NdrFcShort( 0xe ),	/* 14 */
/* 2036 */	NdrFcShort( 0x40 ),	/* 64 */
/* 2038 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x6,		/* 6 */
/* 2040 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2042 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2044 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2046 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2048 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 2050 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2052 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2054 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter boundaryType */

/* 2056 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2058 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2060 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter startOffset */

/* 2062 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2064 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2066 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2068 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2070 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2072 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter text */

/* 2074 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 2076 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2078 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 2080 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2082 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 2084 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_textAtOffset */

/* 2086 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2088 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2092 */	NdrFcShort( 0xd ),	/* 13 */
/* 2094 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 2096 */	NdrFcShort( 0xe ),	/* 14 */
/* 2098 */	NdrFcShort( 0x40 ),	/* 64 */
/* 2100 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x6,		/* 6 */
/* 2102 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2104 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2106 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2108 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2110 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 2112 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2114 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2116 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter boundaryType */

/* 2118 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2120 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2122 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter startOffset */

/* 2124 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2126 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2128 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2130 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2132 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2134 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter text */

/* 2136 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 2138 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2140 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 2142 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2144 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 2146 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure removeSelection */

/* 2148 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2150 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2154 */	NdrFcShort( 0xe ),	/* 14 */
/* 2156 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2158 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2160 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2162 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2164 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2166 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2168 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2170 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2172 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter selectionIndex */

/* 2174 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2176 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2178 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2180 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2182 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2184 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure setCaretOffset */

/* 2186 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2188 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2192 */	NdrFcShort( 0xf ),	/* 15 */
/* 2194 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2196 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2198 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2200 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2202 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2204 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2206 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2208 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2210 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 2212 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2214 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2216 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2218 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2220 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2222 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure setSelection */

/* 2224 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2226 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2230 */	NdrFcShort( 0x10 ),	/* 16 */
/* 2232 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2234 */	NdrFcShort( 0x18 ),	/* 24 */
/* 2236 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2238 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 2240 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2242 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2244 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2246 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2248 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter selectionIndex */

/* 2250 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2252 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2254 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter startOffset */

/* 2256 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2258 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2260 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2262 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2264 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2266 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2268 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2270 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2272 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nCharacters */

/* 2274 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2276 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2280 */	NdrFcShort( 0x11 ),	/* 17 */
/* 2282 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2284 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2286 */	NdrFcShort( 0x24 ),	/* 36 */
/* 2288 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2290 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2292 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2294 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2296 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2298 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter nCharacters */

/* 2300 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2302 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2304 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2306 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2308 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2310 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure scrollSubstringTo */

/* 2312 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2314 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2318 */	NdrFcShort( 0x12 ),	/* 18 */
/* 2320 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2322 */	NdrFcShort( 0x16 ),	/* 22 */
/* 2324 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2326 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 2328 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2330 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2332 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2334 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2336 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startIndex */

/* 2338 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2340 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2342 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endIndex */

/* 2344 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2346 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2348 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter scrollType */

/* 2350 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2352 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2354 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Return value */

/* 2356 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2358 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2360 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure scrollSubstringToPoint */

/* 2362 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2364 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2368 */	NdrFcShort( 0x13 ),	/* 19 */
/* 2370 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 2372 */	NdrFcShort( 0x26 ),	/* 38 */
/* 2374 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2376 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x6,		/* 6 */
/* 2378 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2380 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2382 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2384 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2386 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startIndex */

/* 2388 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2390 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2392 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endIndex */

/* 2394 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2396 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2398 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter coordinateType */

/* 2400 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2402 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2404 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter x */

/* 2406 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2408 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2410 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter y */

/* 2412 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2414 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2416 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2418 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2420 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 2422 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_newText */

/* 2424 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2426 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2430 */	NdrFcShort( 0x14 ),	/* 20 */
/* 2432 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2434 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2436 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2438 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 2440 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2442 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2444 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2446 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2448 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter newText */

/* 2450 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 2452 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2454 */	NdrFcShort( 0x4fa ),	/* Type Offset=1274 */

	/* Return value */

/* 2456 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2458 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2460 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_oldText */

/* 2462 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2464 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2468 */	NdrFcShort( 0x15 ),	/* 21 */
/* 2470 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2472 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2474 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2476 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 2478 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2480 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2482 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2484 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2486 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter oldText */

/* 2488 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 2490 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2492 */	NdrFcShort( 0x4fa ),	/* Type Offset=1274 */

	/* Return value */

/* 2494 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2496 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2498 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_attributeRange */

/* 2500 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2502 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2506 */	NdrFcShort( 0x16 ),	/* 22 */
/* 2508 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 2510 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2512 */	NdrFcShort( 0x40 ),	/* 64 */
/* 2514 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x6,		/* 6 */
/* 2516 */	0xa,		/* 10 */
			0x7,		/* Ext Flags:  new corr desc, clt corr check, srv corr check, */
/* 2518 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2520 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2522 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2524 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 2526 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2528 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2530 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter filter */

/* 2532 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 2534 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2536 */	NdrFcShort( 0x10e ),	/* Type Offset=270 */

	/* Parameter startOffset */

/* 2538 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2540 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2542 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2544 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2546 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2548 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter attributeValues */

/* 2550 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 2552 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2554 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 2556 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2558 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 2560 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure deleteText */

/* 2562 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2564 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2568 */	NdrFcShort( 0x4 ),	/* 4 */
/* 2570 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2572 */	NdrFcShort( 0x10 ),	/* 16 */
/* 2574 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2576 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 2578 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2580 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2582 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2584 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2586 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startOffset */

/* 2588 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2590 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2592 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2594 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2596 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2598 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2600 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2602 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2604 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure insertText */

/* 2606 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2608 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2612 */	NdrFcShort( 0x5 ),	/* 5 */
/* 2614 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2616 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2618 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2620 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 2622 */	0xa,		/* 10 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2624 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2626 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2628 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2630 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 2632 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2634 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2636 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter text */

/* 2638 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2640 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2642 */	NdrFcShort( 0x10e ),	/* Type Offset=270 */

	/* Return value */

/* 2644 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2646 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2648 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure cutText */

/* 2650 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2652 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2656 */	NdrFcShort( 0x6 ),	/* 6 */
/* 2658 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2660 */	NdrFcShort( 0x10 ),	/* 16 */
/* 2662 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2664 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 2666 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2668 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2670 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2672 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2674 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startOffset */

/* 2676 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2678 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2680 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2682 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2684 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2686 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2688 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2690 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2692 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure pasteText */

/* 2694 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2696 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2700 */	NdrFcShort( 0x7 ),	/* 7 */
/* 2702 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2704 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2706 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2708 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2710 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2712 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2714 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2716 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2718 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter offset */

/* 2720 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2722 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2724 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2726 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2728 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2730 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure replaceText */

/* 2732 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2734 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2738 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2740 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2742 */	NdrFcShort( 0x10 ),	/* 16 */
/* 2744 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2746 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 2748 */	0xa,		/* 10 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2750 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2752 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2754 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2756 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startOffset */

/* 2758 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2760 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2762 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2764 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2766 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2768 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter text */

/* 2770 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2772 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2774 */	NdrFcShort( 0x10e ),	/* Type Offset=270 */

	/* Return value */

/* 2776 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2778 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2780 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure setAttributes */

/* 2782 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2784 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2788 */	NdrFcShort( 0x9 ),	/* 9 */
/* 2790 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 2792 */	NdrFcShort( 0x10 ),	/* 16 */
/* 2794 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2796 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 2798 */	0xa,		/* 10 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2800 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2802 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2804 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2806 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter startOffset */

/* 2808 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2810 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2812 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter endOffset */

/* 2814 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2816 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2818 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter attributes */

/* 2820 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2822 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2824 */	NdrFcShort( 0x10e ),	/* Type Offset=270 */

	/* Return value */

/* 2826 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2828 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2830 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_anchor */

/* 2832 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2834 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2838 */	NdrFcShort( 0x9 ),	/* 9 */
/* 2840 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2842 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2844 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2846 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 2848 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2850 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2852 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2854 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2856 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter index */

/* 2858 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2860 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2862 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter anchor */

/* 2864 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 2866 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2868 */	NdrFcShort( 0x4bc ),	/* Type Offset=1212 */

	/* Return value */

/* 2870 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2872 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2874 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_anchorTarget */

/* 2876 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2878 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2882 */	NdrFcShort( 0xa ),	/* 10 */
/* 2884 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 2886 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2888 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2890 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 2892 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2894 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2896 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2898 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2900 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter index */

/* 2902 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2904 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2906 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter anchorTarget */

/* 2908 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 2910 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2912 */	NdrFcShort( 0x4bc ),	/* Type Offset=1212 */

	/* Return value */

/* 2914 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2916 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2918 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nRows */


	/* Procedure get_startIndex */

/* 2920 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2922 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2926 */	NdrFcShort( 0xb ),	/* 11 */
/* 2928 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2930 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2932 */	NdrFcShort( 0x24 ),	/* 36 */
/* 2934 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2936 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2938 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2940 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2942 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2944 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter rowCount */


	/* Parameter index */

/* 2946 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2948 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2950 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 2952 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2954 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2956 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nSelectedChildren */


	/* Procedure get_endIndex */

/* 2958 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2960 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2964 */	NdrFcShort( 0xc ),	/* 12 */
/* 2966 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 2968 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2970 */	NdrFcShort( 0x24 ),	/* 36 */
/* 2972 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2974 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2976 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2978 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2980 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2982 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cellCount */


	/* Parameter index */

/* 2984 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2986 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 2988 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 2990 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2992 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 2994 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_valid */

/* 2996 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2998 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3002 */	NdrFcShort( 0xd ),	/* 13 */
/* 3004 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3006 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3008 */	NdrFcShort( 0x21 ),	/* 33 */
/* 3010 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 3012 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3014 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3016 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3018 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3020 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter valid */

/* 3022 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3024 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3026 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 3028 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3030 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3032 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nHyperlinks */

/* 3034 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3036 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3040 */	NdrFcShort( 0x16 ),	/* 22 */
/* 3042 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3044 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3046 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3048 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 3050 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3052 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3054 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3056 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3058 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter hyperlinkCount */

/* 3060 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3062 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3064 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3066 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3068 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3070 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_hyperlink */

/* 3072 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3074 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3078 */	NdrFcShort( 0x17 ),	/* 23 */
/* 3080 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3082 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3084 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3086 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 3088 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3090 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3092 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3094 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3096 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter index */

/* 3098 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3100 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3102 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter hyperlink */

/* 3104 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 3106 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3108 */	NdrFcShort( 0x512 ),	/* Type Offset=1298 */

	/* Return value */

/* 3110 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3112 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3114 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_hyperlinkIndex */

/* 3116 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3118 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3122 */	NdrFcShort( 0x18 ),	/* 24 */
/* 3124 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3126 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3128 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3130 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 3132 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3134 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3136 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3138 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3140 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter charIndex */

/* 3142 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3144 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3146 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter hyperlinkIndex */

/* 3148 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3150 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3152 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3154 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3156 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3158 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_hyperlinks */

/* 3160 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3162 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3166 */	NdrFcShort( 0x19 ),	/* 25 */
/* 3168 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3170 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3172 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3174 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 3176 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 3178 */	NdrFcShort( 0x1 ),	/* 1 */
/* 3180 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3182 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3184 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter hyperlinks */

/* 3186 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 3188 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3190 */	NdrFcShort( 0x528 ),	/* Type Offset=1320 */

	/* Parameter nHyperlinks */

/* 3192 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3194 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3196 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3198 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3200 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3202 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_cellAt */


	/* Procedure get_accessibleAt */

/* 3204 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3206 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3210 */	NdrFcShort( 0x3 ),	/* 3 */
/* 3212 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 3214 */	NdrFcShort( 0x10 ),	/* 16 */
/* 3216 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3218 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 3220 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3222 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3224 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3226 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3228 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */


	/* Parameter row */

/* 3230 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3232 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3234 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter column */


	/* Parameter column */

/* 3236 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3238 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3240 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter cell */


	/* Parameter accessible */

/* 3242 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 3244 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3246 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */


	/* Return value */

/* 3248 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3250 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3252 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_caption */


	/* Procedure get_caption */

/* 3254 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3256 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3260 */	NdrFcShort( 0x4 ),	/* 4 */
/* 3262 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3264 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3266 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3268 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 3270 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3272 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3274 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3276 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3278 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter accessible */


	/* Parameter accessible */

/* 3280 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 3282 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3284 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */


	/* Return value */

/* 3286 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3288 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3290 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_childIndex */

/* 3292 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3294 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3298 */	NdrFcShort( 0x5 ),	/* 5 */
/* 3300 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 3302 */	NdrFcShort( 0x10 ),	/* 16 */
/* 3304 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3306 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 3308 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3310 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3312 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3314 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3316 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter rowIndex */

/* 3318 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3320 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3322 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter columnIndex */

/* 3324 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3326 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3328 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter cellIndex */

/* 3330 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3332 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3334 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3336 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3338 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3340 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnDescription */

/* 3342 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3344 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3348 */	NdrFcShort( 0x6 ),	/* 6 */
/* 3350 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3352 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3354 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3356 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 3358 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 3360 */	NdrFcShort( 0x1 ),	/* 1 */
/* 3362 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3364 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3366 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */

/* 3368 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3370 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3372 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter description */

/* 3374 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 3376 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3378 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 3380 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3382 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3384 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnExtentAt */

/* 3386 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3388 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3392 */	NdrFcShort( 0x7 ),	/* 7 */
/* 3394 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 3396 */	NdrFcShort( 0x10 ),	/* 16 */
/* 3398 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3400 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 3402 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3404 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3406 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3408 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3410 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 3412 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3414 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3416 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter column */

/* 3418 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3420 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3422 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter nColumnsSpanned */

/* 3424 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3426 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3428 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3430 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3432 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3434 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnHeader */

/* 3436 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3438 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3442 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3444 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3446 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3448 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3450 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 3452 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3454 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3456 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3458 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3460 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter accessibleTable */

/* 3462 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 3464 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3466 */	NdrFcShort( 0x546 ),	/* Type Offset=1350 */

	/* Parameter startingRowIndex */

/* 3468 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3470 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3472 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3474 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3476 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3478 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnIndex */

/* 3480 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3482 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3486 */	NdrFcShort( 0x9 ),	/* 9 */
/* 3488 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3490 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3492 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3494 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 3496 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3498 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3500 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3502 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3504 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cellIndex */

/* 3506 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3508 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3510 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter columnIndex */

/* 3512 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3514 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3516 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3518 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3520 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3522 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nSelectedRows */


	/* Procedure get_nColumns */

/* 3524 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3526 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3530 */	NdrFcShort( 0xa ),	/* 10 */
/* 3532 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3534 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3536 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3538 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 3540 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3542 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3544 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3546 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3548 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter rowCount */


	/* Parameter columnCount */

/* 3550 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3552 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3554 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 3556 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3558 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3560 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nSelectedColumns */

/* 3562 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3564 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3568 */	NdrFcShort( 0xd ),	/* 13 */
/* 3570 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3572 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3574 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3576 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 3578 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3580 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3582 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3584 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3586 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter columnCount */

/* 3588 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3590 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3592 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3594 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3596 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3598 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nSelectedRows */

/* 3600 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3602 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3606 */	NdrFcShort( 0xe ),	/* 14 */
/* 3608 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3610 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3612 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3614 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 3616 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3618 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3620 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3622 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3624 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter rowCount */

/* 3626 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3628 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3630 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3632 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3634 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3636 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowDescription */

/* 3638 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3640 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3644 */	NdrFcShort( 0xf ),	/* 15 */
/* 3646 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3648 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3650 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3652 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 3654 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 3656 */	NdrFcShort( 0x1 ),	/* 1 */
/* 3658 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3660 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3662 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 3664 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3666 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3668 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter description */

/* 3670 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 3672 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3674 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 3676 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3678 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3680 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowExtentAt */

/* 3682 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3684 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3688 */	NdrFcShort( 0x10 ),	/* 16 */
/* 3690 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 3692 */	NdrFcShort( 0x10 ),	/* 16 */
/* 3694 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3696 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 3698 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3700 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3702 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3704 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3706 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 3708 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3710 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3712 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter column */

/* 3714 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3716 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3718 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter nRowsSpanned */

/* 3720 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3722 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3724 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3726 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3728 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3730 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowHeader */

/* 3732 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3734 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3738 */	NdrFcShort( 0x11 ),	/* 17 */
/* 3740 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3742 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3744 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3746 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 3748 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3750 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3752 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3754 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3756 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter accessibleTable */

/* 3758 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 3760 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3762 */	NdrFcShort( 0x546 ),	/* Type Offset=1350 */

	/* Parameter startingColumnIndex */

/* 3764 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3766 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3768 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3770 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3772 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3774 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowIndex */

/* 3776 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3778 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3782 */	NdrFcShort( 0x12 ),	/* 18 */
/* 3784 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3786 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3788 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3790 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 3792 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3794 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3796 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3798 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3800 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cellIndex */

/* 3802 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3804 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3806 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter rowIndex */

/* 3808 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3810 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3812 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3814 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3816 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3818 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_selectedChildren */

/* 3820 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3822 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3826 */	NdrFcShort( 0x13 ),	/* 19 */
/* 3828 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 3830 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3832 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3834 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 3836 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 3838 */	NdrFcShort( 0x1 ),	/* 1 */
/* 3840 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3842 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3844 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maxChildren */

/* 3846 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3848 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3850 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter children */

/* 3852 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 3854 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3856 */	NdrFcShort( 0x55c ),	/* Type Offset=1372 */

	/* Parameter nChildren */

/* 3858 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3860 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3862 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3864 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3866 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3868 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_selectedColumns */

/* 3870 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3872 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3876 */	NdrFcShort( 0x14 ),	/* 20 */
/* 3878 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 3880 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3882 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3884 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 3886 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 3888 */	NdrFcShort( 0x1 ),	/* 1 */
/* 3890 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3892 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3894 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maxColumns */

/* 3896 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3898 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3900 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter columns */

/* 3902 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 3904 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3906 */	NdrFcShort( 0x55c ),	/* Type Offset=1372 */

	/* Parameter nColumns */

/* 3908 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3910 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3912 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3914 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3916 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3918 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_selectedRows */

/* 3920 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3922 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3926 */	NdrFcShort( 0x15 ),	/* 21 */
/* 3928 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 3930 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3932 */	NdrFcShort( 0x24 ),	/* 36 */
/* 3934 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x4,		/* 4 */
/* 3936 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 3938 */	NdrFcShort( 0x1 ),	/* 1 */
/* 3940 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3942 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3944 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter maxRows */

/* 3946 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 3948 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 3950 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter rows */

/* 3952 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 3954 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 3956 */	NdrFcShort( 0x55c ),	/* Type Offset=1372 */

	/* Parameter nRows */

/* 3958 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 3960 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3962 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 3964 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 3966 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 3968 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_summary */

/* 3970 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 3972 */	NdrFcLong( 0x0 ),	/* 0 */
/* 3976 */	NdrFcShort( 0x16 ),	/* 22 */
/* 3978 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 3980 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3982 */	NdrFcShort( 0x8 ),	/* 8 */
/* 3984 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 3986 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 3988 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3990 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3992 */	NdrFcShort( 0x0 ),	/* 0 */
/* 3994 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter accessible */

/* 3996 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 3998 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4000 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 4002 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4004 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4006 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_isColumnSelected */

/* 4008 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4010 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4014 */	NdrFcShort( 0x17 ),	/* 23 */
/* 4016 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4018 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4020 */	NdrFcShort( 0x21 ),	/* 33 */
/* 4022 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 4024 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4026 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4028 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4030 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4032 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */

/* 4034 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4036 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4038 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter isSelected */

/* 4040 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4042 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4044 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 4046 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4048 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4050 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_isRowSelected */

/* 4052 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4054 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4058 */	NdrFcShort( 0x18 ),	/* 24 */
/* 4060 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4062 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4064 */	NdrFcShort( 0x21 ),	/* 33 */
/* 4066 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 4068 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4070 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4072 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4074 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4076 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4078 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4080 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4082 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter isSelected */

/* 4084 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4086 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4088 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 4090 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4092 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4094 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_isSelected */

/* 4096 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4098 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4102 */	NdrFcShort( 0x19 ),	/* 25 */
/* 4104 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 4106 */	NdrFcShort( 0x10 ),	/* 16 */
/* 4108 */	NdrFcShort( 0x21 ),	/* 33 */
/* 4110 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 4112 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4114 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4116 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4118 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4120 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4122 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4124 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4126 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter column */

/* 4128 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4130 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4132 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter isSelected */

/* 4134 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4136 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4138 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 4140 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4142 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4144 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure selectRow */

/* 4146 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4148 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4152 */	NdrFcShort( 0x1a ),	/* 26 */
/* 4154 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4156 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4158 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4160 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4162 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4164 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4166 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4168 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4170 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4172 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4174 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4176 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4178 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4180 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4182 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure selectColumn */

/* 4184 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4186 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4190 */	NdrFcShort( 0x1b ),	/* 27 */
/* 4192 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4194 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4196 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4198 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4200 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4202 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4204 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4206 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4208 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */

/* 4210 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4212 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4214 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4216 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4218 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4220 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure unselectRow */

/* 4222 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4224 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4228 */	NdrFcShort( 0x1c ),	/* 28 */
/* 4230 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4232 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4234 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4236 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4238 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4240 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4242 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4244 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4246 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4248 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4250 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4252 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4254 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4256 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4258 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure unselectColumn */

/* 4260 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4262 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4266 */	NdrFcShort( 0x1d ),	/* 29 */
/* 4268 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4270 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4272 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4274 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4276 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4278 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4280 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4282 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4284 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */

/* 4286 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4288 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4290 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4292 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4294 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4296 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowColumnExtentsAtIndex */

/* 4298 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4300 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4304 */	NdrFcShort( 0x1e ),	/* 30 */
/* 4306 */	NdrFcShort( 0x40 ),	/* X64 Stack size/offset = 64 */
/* 4308 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4310 */	NdrFcShort( 0x91 ),	/* 145 */
/* 4312 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x7,		/* 7 */
/* 4314 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4316 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4318 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4320 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4322 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter index */

/* 4324 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4326 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4328 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter row */

/* 4330 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4332 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4334 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter column */

/* 4336 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4338 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4340 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter rowExtents */

/* 4342 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4344 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4346 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter columnExtents */

/* 4348 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4350 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 4352 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter isSelected */

/* 4354 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4356 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 4358 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 4360 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4362 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 4364 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_modelChange */

/* 4366 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4368 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4372 */	NdrFcShort( 0x1f ),	/* 31 */
/* 4374 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4376 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4378 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4380 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 4382 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4384 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4386 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4388 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4390 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter modelChange */

/* 4392 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 4394 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4396 */	NdrFcShort( 0x57a ),	/* Type Offset=1402 */

	/* Return value */

/* 4398 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4400 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4402 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowExtent */


	/* Procedure get_nColumns */

/* 4404 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4406 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4410 */	NdrFcShort( 0x6 ),	/* 6 */
/* 4412 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4414 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4416 */	NdrFcShort( 0x24 ),	/* 36 */
/* 4418 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4420 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4422 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4424 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4426 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4428 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter nRowsSpanned */


	/* Parameter columnCount */

/* 4430 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4432 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4434 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 4436 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4438 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4440 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowIndex */


	/* Procedure get_nSelectedCells */

/* 4442 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4444 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4448 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4450 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4452 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4454 */	NdrFcShort( 0x24 ),	/* 36 */
/* 4456 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4458 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4460 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4462 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4464 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4466 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter rowIndex */


	/* Parameter cellCount */

/* 4468 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4470 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4472 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 4474 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4476 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4478 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_nSelectedColumns */

/* 4480 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4482 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4486 */	NdrFcShort( 0x9 ),	/* 9 */
/* 4488 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4490 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4492 */	NdrFcShort( 0x24 ),	/* 36 */
/* 4494 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4496 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4498 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4500 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4502 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4504 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter columnCount */

/* 4506 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4508 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4510 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4512 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4514 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4516 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowDescription */

/* 4518 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4520 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4524 */	NdrFcShort( 0xb ),	/* 11 */
/* 4526 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4528 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4530 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4532 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 4534 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 4536 */	NdrFcShort( 0x1 ),	/* 1 */
/* 4538 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4540 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4542 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4544 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4546 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4548 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter description */

/* 4550 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 4552 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4554 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 4556 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4558 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4560 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_selectedCells */

/* 4562 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4564 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4568 */	NdrFcShort( 0xc ),	/* 12 */
/* 4570 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4572 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4574 */	NdrFcShort( 0x24 ),	/* 36 */
/* 4576 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 4578 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 4580 */	NdrFcShort( 0x1 ),	/* 1 */
/* 4582 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4584 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4586 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cells */

/* 4588 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 4590 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4592 */	NdrFcShort( 0x588 ),	/* Type Offset=1416 */

	/* Parameter nSelectedCells */

/* 4594 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4596 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4598 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4600 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4602 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4604 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_selectedColumns */

/* 4606 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4608 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4612 */	NdrFcShort( 0xd ),	/* 13 */
/* 4614 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4616 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4618 */	NdrFcShort( 0x24 ),	/* 36 */
/* 4620 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 4622 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 4624 */	NdrFcShort( 0x1 ),	/* 1 */
/* 4626 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4628 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4630 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter selectedColumns */

/* 4632 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 4634 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4636 */	NdrFcShort( 0x5a6 ),	/* Type Offset=1446 */

	/* Parameter nColumns */

/* 4638 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4640 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4642 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4644 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4646 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4648 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_selectedRows */

/* 4650 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4652 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4656 */	NdrFcShort( 0xe ),	/* 14 */
/* 4658 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4660 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4662 */	NdrFcShort( 0x24 ),	/* 36 */
/* 4664 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 4666 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 4668 */	NdrFcShort( 0x1 ),	/* 1 */
/* 4670 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4672 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4674 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter selectedRows */

/* 4676 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 4678 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4680 */	NdrFcShort( 0x5a6 ),	/* Type Offset=1446 */

	/* Parameter nRows */

/* 4682 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4684 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4686 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4688 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4690 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4692 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_summary */

/* 4694 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4696 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4700 */	NdrFcShort( 0xf ),	/* 15 */
/* 4702 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4704 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4706 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4708 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 4710 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4712 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4714 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4716 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4718 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter accessible */

/* 4720 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 4722 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4724 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 4726 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4728 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4730 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_isColumnSelected */

/* 4732 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4734 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4738 */	NdrFcShort( 0x10 ),	/* 16 */
/* 4740 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4742 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4744 */	NdrFcShort( 0x21 ),	/* 33 */
/* 4746 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 4748 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4750 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4752 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4754 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4756 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */

/* 4758 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4760 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4762 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter isSelected */

/* 4764 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4766 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4768 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 4770 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4772 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4774 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_isRowSelected */

/* 4776 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4778 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4782 */	NdrFcShort( 0x11 ),	/* 17 */
/* 4784 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 4786 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4788 */	NdrFcShort( 0x21 ),	/* 33 */
/* 4790 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 4792 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4794 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4796 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4798 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4800 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4802 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4804 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4806 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter isSelected */

/* 4808 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 4810 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4812 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 4814 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4816 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4818 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure selectRow */

/* 4820 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4822 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4826 */	NdrFcShort( 0x12 ),	/* 18 */
/* 4828 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4830 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4832 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4834 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4836 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4838 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4840 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4842 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4844 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4846 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4848 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4850 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4852 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4854 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4856 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure selectColumn */

/* 4858 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4860 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4864 */	NdrFcShort( 0x13 ),	/* 19 */
/* 4866 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4868 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4870 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4872 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4874 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4876 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4878 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4880 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4882 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */

/* 4884 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4886 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4888 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4890 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4892 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4894 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure unselectRow */

/* 4896 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4898 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4902 */	NdrFcShort( 0x14 ),	/* 20 */
/* 4904 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4906 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4908 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4910 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4912 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4914 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4916 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4918 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4920 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 4922 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4924 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4926 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4928 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4930 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4932 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure unselectColumn */

/* 4934 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4936 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4940 */	NdrFcShort( 0x15 ),	/* 21 */
/* 4942 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4944 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4946 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4948 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 4950 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4952 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4954 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4956 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4958 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter column */

/* 4960 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 4962 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 4964 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 4966 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 4968 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 4970 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_modelChange */

/* 4972 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 4974 */	NdrFcLong( 0x0 ),	/* 0 */
/* 4978 */	NdrFcShort( 0x16 ),	/* 22 */
/* 4980 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 4982 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4984 */	NdrFcShort( 0x8 ),	/* 8 */
/* 4986 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 4988 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 4990 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4992 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4994 */	NdrFcShort( 0x0 ),	/* 0 */
/* 4996 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter modelChange */

/* 4998 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 5000 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5002 */	NdrFcShort( 0x57a ),	/* Type Offset=1402 */

	/* Return value */

/* 5004 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5006 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5008 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_columnHeaderCells */

/* 5010 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5012 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5016 */	NdrFcShort( 0x4 ),	/* 4 */
/* 5018 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 5020 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5022 */	NdrFcShort( 0x24 ),	/* 36 */
/* 5024 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 5026 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 5028 */	NdrFcShort( 0x1 ),	/* 1 */
/* 5030 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5032 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5034 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cellAccessibles */

/* 5036 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 5038 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5040 */	NdrFcShort( 0x588 ),	/* Type Offset=1416 */

	/* Parameter nColumnHeaderCells */

/* 5042 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5044 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5046 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 5048 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5050 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5052 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowHeaderCells */

/* 5054 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5056 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5060 */	NdrFcShort( 0x7 ),	/* 7 */
/* 5062 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 5064 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5066 */	NdrFcShort( 0x24 ),	/* 36 */
/* 5068 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 5070 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 5072 */	NdrFcShort( 0x1 ),	/* 1 */
/* 5074 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5076 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5078 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cellAccessibles */

/* 5080 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 5082 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5084 */	NdrFcShort( 0x588 ),	/* Type Offset=1416 */

	/* Parameter nRowHeaderCells */

/* 5086 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5088 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5090 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 5092 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5094 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5096 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_isSelected */

/* 5098 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5100 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5104 */	NdrFcShort( 0x9 ),	/* 9 */
/* 5106 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5108 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5110 */	NdrFcShort( 0x21 ),	/* 33 */
/* 5112 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 5114 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 5116 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5118 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5120 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5122 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter isSelected */

/* 5124 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5126 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5128 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 5130 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5132 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5134 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_rowColumnExtents */

/* 5136 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5138 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5142 */	NdrFcShort( 0xa ),	/* 10 */
/* 5144 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 5146 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5148 */	NdrFcShort( 0x91 ),	/* 145 */
/* 5150 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x6,		/* 6 */
/* 5152 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 5154 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5156 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5158 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5160 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter row */

/* 5162 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5164 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5166 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter column */

/* 5168 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5170 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5172 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter rowExtents */

/* 5174 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5176 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5178 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter columnExtents */

/* 5180 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5182 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 5184 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter isSelected */

/* 5186 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5188 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 5190 */	0x3,		/* FC_SMALL */
			0x0,		/* 0 */

	/* Return value */

/* 5192 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5194 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 5196 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_table */

/* 5198 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5200 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5204 */	NdrFcShort( 0xb ),	/* 11 */
/* 5206 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5208 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5210 */	NdrFcShort( 0x8 ),	/* 8 */
/* 5212 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 5214 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 5216 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5218 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5220 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5222 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter table */

/* 5224 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 5226 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5228 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 5230 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5232 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5234 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_imagePosition */

/* 5236 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5238 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5242 */	NdrFcShort( 0x4 ),	/* 4 */
/* 5244 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 5246 */	NdrFcShort( 0x6 ),	/* 6 */
/* 5248 */	NdrFcShort( 0x40 ),	/* 64 */
/* 5250 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x4,		/* 4 */
/* 5252 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 5254 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5256 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5258 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5260 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter coordinateType */

/* 5262 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 5264 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5266 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Parameter x */

/* 5268 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5270 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5272 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter y */

/* 5274 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5276 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5278 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 5280 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5282 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 5284 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_imageSize */

/* 5286 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5288 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5292 */	NdrFcShort( 0x5 ),	/* 5 */
/* 5294 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 5296 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5298 */	NdrFcShort( 0x40 ),	/* 64 */
/* 5300 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 5302 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 5304 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5306 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5308 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5310 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter height */

/* 5312 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5314 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5316 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter width */

/* 5318 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 5320 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5322 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 5324 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5326 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5328 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_toolkitName */

/* 5330 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5332 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5336 */	NdrFcShort( 0x5 ),	/* 5 */
/* 5338 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5340 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5342 */	NdrFcShort( 0x8 ),	/* 8 */
/* 5344 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 5346 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 5348 */	NdrFcShort( 0x1 ),	/* 1 */
/* 5350 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5352 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5354 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter name */

/* 5356 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 5358 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5360 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 5362 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5364 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5366 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_toolkitVersion */

/* 5368 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5370 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5374 */	NdrFcShort( 0x6 ),	/* 6 */
/* 5376 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5378 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5380 */	NdrFcShort( 0x8 ),	/* 8 */
/* 5382 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 5384 */	0xa,		/* 10 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 5386 */	NdrFcShort( 0x1 ),	/* 1 */
/* 5388 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5390 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5392 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter version */

/* 5394 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 5396 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5398 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 5400 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5402 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5404 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_anchorTarget */

/* 5406 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 5408 */	NdrFcLong( 0x0 ),	/* 0 */
/* 5412 */	NdrFcShort( 0x3 ),	/* 3 */
/* 5414 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 5416 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5418 */	NdrFcShort( 0x8 ),	/* 8 */
/* 5420 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 5422 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 5424 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5426 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5428 */	NdrFcShort( 0x0 ),	/* 0 */
/* 5430 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter accessible */

/* 5432 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 5434 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 5436 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 5438 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 5440 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 5442 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

			0x0
        }
    };

static const ia2_api_all_MIDL_TYPE_FORMAT_STRING ia2_api_all__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */
/*  2 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/*  4 */	NdrFcShort( 0x1c ),	/* Offset= 28 (32) */
/*  6 */	
			0x13, 0x0,	/* FC_OP */
/*  8 */	NdrFcShort( 0xe ),	/* Offset= 14 (22) */
/* 10 */	
			0x1b,		/* FC_CARRAY */
			0x1,		/* 1 */
/* 12 */	NdrFcShort( 0x2 ),	/* 2 */
/* 14 */	0x9,		/* Corr desc: FC_ULONG */
			0x0,		/*  */
/* 16 */	NdrFcShort( 0xfffc ),	/* -4 */
/* 18 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 20 */	0x6,		/* FC_SHORT */
			0x5b,		/* FC_END */
/* 22 */	
			0x17,		/* FC_CSTRUCT */
			0x3,		/* 3 */
/* 24 */	NdrFcShort( 0x8 ),	/* 8 */
/* 26 */	NdrFcShort( 0xfff0 ),	/* Offset= -16 (10) */
/* 28 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 30 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 32 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 34 */	NdrFcShort( 0x0 ),	/* 0 */
/* 36 */	NdrFcShort( 0x8 ),	/* 8 */
/* 38 */	NdrFcShort( 0x0 ),	/* 0 */
/* 40 */	NdrFcShort( 0xffde ),	/* Offset= -34 (6) */
/* 42 */	
			0x11, 0xc,	/* FC_RP [alloced_on_stack] [simple_pointer] */
/* 44 */	0x8,		/* FC_LONG */
			0x5c,		/* FC_PAD */
/* 46 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 48 */	NdrFcShort( 0x2 ),	/* Offset= 2 (50) */
/* 50 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 52 */	NdrFcLong( 0x0 ),	/* 0 */
/* 56 */	NdrFcShort( 0x0 ),	/* 0 */
/* 58 */	NdrFcShort( 0x0 ),	/* 0 */
/* 60 */	0xc0,		/* 192 */
			0x0,		/* 0 */
/* 62 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 64 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 66 */	0x0,		/* 0 */
			0x46,		/* 70 */
/* 68 */	
			0x11, 0x0,	/* FC_RP */
/* 70 */	NdrFcShort( 0x2 ),	/* Offset= 2 (72) */
/* 72 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 74 */	NdrFcShort( 0x0 ),	/* 0 */
/* 76 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x0,		/*  */
/* 78 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 80 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 82 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 84 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 86 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 88 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 90 */	NdrFcShort( 0xffd8 ),	/* Offset= -40 (50) */
/* 92 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 94 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/* 96 */	NdrFcShort( 0x2 ),	/* Offset= 2 (98) */
/* 98 */	
			0x13, 0x0,	/* FC_OP */
/* 100 */	NdrFcShort( 0x2 ),	/* Offset= 2 (102) */
/* 102 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 104 */	NdrFcShort( 0x0 ),	/* 0 */
/* 106 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x0,		/*  */
/* 108 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 110 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 112 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 114 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 116 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 118 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 120 */	NdrFcShort( 0xffa8 ),	/* Offset= -88 (32) */
/* 122 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 124 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 126 */	NdrFcShort( 0x2 ),	/* Offset= 2 (128) */
/* 128 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 130 */	NdrFcLong( 0x7cdf86ee ),	/* 2095023854 */
/* 134 */	NdrFcShort( 0xc3da ),	/* -15398 */
/* 136 */	NdrFcShort( 0x496a ),	/* 18794 */
/* 138 */	0xbd,		/* 189 */
			0xa4,		/* 164 */
/* 140 */	0x28,		/* 40 */
			0x1b,		/* 27 */
/* 142 */	0x33,		/* 51 */
			0x6e,		/* 110 */
/* 144 */	0x1f,		/* 31 */
			0xdc,		/* 220 */
/* 146 */	
			0x11, 0x0,	/* FC_RP */
/* 148 */	NdrFcShort( 0x2 ),	/* Offset= 2 (150) */
/* 150 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 152 */	NdrFcShort( 0x0 ),	/* 0 */
/* 154 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x0,		/*  */
/* 156 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 158 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 160 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 162 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 164 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 166 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 168 */	NdrFcShort( 0xffd8 ),	/* Offset= -40 (128) */
/* 170 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 172 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/* 174 */	NdrFcShort( 0x2 ),	/* Offset= 2 (176) */
/* 176 */	
			0x13, 0x0,	/* FC_OP */
/* 178 */	NdrFcShort( 0x2 ),	/* Offset= 2 (180) */
/* 180 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 182 */	NdrFcShort( 0x0 ),	/* 0 */
/* 184 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x0,		/*  */
/* 186 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 188 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 190 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 192 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 194 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 196 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 198 */	NdrFcShort( 0xff5a ),	/* Offset= -166 (32) */
/* 200 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 202 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 204 */	NdrFcShort( 0x1a ),	/* Offset= 26 (230) */
/* 206 */	
			0x13, 0x0,	/* FC_OP */
/* 208 */	NdrFcShort( 0x2 ),	/* Offset= 2 (210) */
/* 210 */	
			0x2a,		/* FC_ENCAPSULATED_UNION */
			0x48,		/* 72 */
/* 212 */	NdrFcShort( 0x4 ),	/* 4 */
/* 214 */	NdrFcShort( 0x2 ),	/* 2 */
/* 216 */	NdrFcLong( 0x48746457 ),	/* 1215587415 */
/* 220 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 222 */	NdrFcLong( 0x52746457 ),	/* 1383359575 */
/* 226 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 228 */	NdrFcShort( 0xffff ),	/* Offset= -1 (227) */
/* 230 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 232 */	NdrFcShort( 0x1 ),	/* 1 */
/* 234 */	NdrFcShort( 0x8 ),	/* 8 */
/* 236 */	NdrFcShort( 0x0 ),	/* 0 */
/* 238 */	NdrFcShort( 0xffe0 ),	/* Offset= -32 (206) */
/* 240 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 242 */	NdrFcShort( 0x2 ),	/* Offset= 2 (244) */
/* 244 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 246 */	NdrFcShort( 0x18 ),	/* 24 */
/* 248 */	NdrFcShort( 0x0 ),	/* 0 */
/* 250 */	NdrFcShort( 0x0 ),	/* Offset= 0 (250) */
/* 252 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 254 */	NdrFcShort( 0xff22 ),	/* Offset= -222 (32) */
/* 256 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 258 */	NdrFcShort( 0xff1e ),	/* Offset= -226 (32) */
/* 260 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 262 */	NdrFcShort( 0xff1a ),	/* Offset= -230 (32) */
/* 264 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 266 */	
			0x12, 0x0,	/* FC_UP */
/* 268 */	NdrFcShort( 0xff0a ),	/* Offset= -246 (22) */
/* 270 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 272 */	NdrFcShort( 0x0 ),	/* 0 */
/* 274 */	NdrFcShort( 0x8 ),	/* 8 */
/* 276 */	NdrFcShort( 0x0 ),	/* 0 */
/* 278 */	NdrFcShort( 0xfff4 ),	/* Offset= -12 (266) */
/* 280 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 282 */	NdrFcShort( 0x3a2 ),	/* Offset= 930 (1212) */
/* 284 */	
			0x13, 0x0,	/* FC_OP */
/* 286 */	NdrFcShort( 0x38a ),	/* Offset= 906 (1192) */
/* 288 */	
			0x2b,		/* FC_NON_ENCAPSULATED_UNION */
			0x9,		/* FC_ULONG */
/* 290 */	0x7,		/* Corr desc: FC_USHORT */
			0x0,		/*  */
/* 292 */	NdrFcShort( 0xfff8 ),	/* -8 */
/* 294 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 296 */	NdrFcShort( 0x2 ),	/* Offset= 2 (298) */
/* 298 */	NdrFcShort( 0x10 ),	/* 16 */
/* 300 */	NdrFcShort( 0x2f ),	/* 47 */
/* 302 */	NdrFcLong( 0x14 ),	/* 20 */
/* 306 */	NdrFcShort( 0x800b ),	/* Simple arm type: FC_HYPER */
/* 308 */	NdrFcLong( 0x3 ),	/* 3 */
/* 312 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 314 */	NdrFcLong( 0x11 ),	/* 17 */
/* 318 */	NdrFcShort( 0x8001 ),	/* Simple arm type: FC_BYTE */
/* 320 */	NdrFcLong( 0x2 ),	/* 2 */
/* 324 */	NdrFcShort( 0x8006 ),	/* Simple arm type: FC_SHORT */
/* 326 */	NdrFcLong( 0x4 ),	/* 4 */
/* 330 */	NdrFcShort( 0x800a ),	/* Simple arm type: FC_FLOAT */
/* 332 */	NdrFcLong( 0x5 ),	/* 5 */
/* 336 */	NdrFcShort( 0x800c ),	/* Simple arm type: FC_DOUBLE */
/* 338 */	NdrFcLong( 0xb ),	/* 11 */
/* 342 */	NdrFcShort( 0x8006 ),	/* Simple arm type: FC_SHORT */
/* 344 */	NdrFcLong( 0xa ),	/* 10 */
/* 348 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 350 */	NdrFcLong( 0x6 ),	/* 6 */
/* 354 */	NdrFcShort( 0xe8 ),	/* Offset= 232 (586) */
/* 356 */	NdrFcLong( 0x7 ),	/* 7 */
/* 360 */	NdrFcShort( 0x800c ),	/* Simple arm type: FC_DOUBLE */
/* 362 */	NdrFcLong( 0x8 ),	/* 8 */
/* 366 */	NdrFcShort( 0xfe98 ),	/* Offset= -360 (6) */
/* 368 */	NdrFcLong( 0xd ),	/* 13 */
/* 372 */	NdrFcShort( 0xfebe ),	/* Offset= -322 (50) */
/* 374 */	NdrFcLong( 0x9 ),	/* 9 */
/* 378 */	NdrFcShort( 0xd6 ),	/* Offset= 214 (592) */
/* 380 */	NdrFcLong( 0x2000 ),	/* 8192 */
/* 384 */	NdrFcShort( 0xe2 ),	/* Offset= 226 (610) */
/* 386 */	NdrFcLong( 0x24 ),	/* 36 */
/* 390 */	NdrFcShort( 0x2d8 ),	/* Offset= 728 (1118) */
/* 392 */	NdrFcLong( 0x4024 ),	/* 16420 */
/* 396 */	NdrFcShort( 0x2d2 ),	/* Offset= 722 (1118) */
/* 398 */	NdrFcLong( 0x4011 ),	/* 16401 */
/* 402 */	NdrFcShort( 0x2d0 ),	/* Offset= 720 (1122) */
/* 404 */	NdrFcLong( 0x4002 ),	/* 16386 */
/* 408 */	NdrFcShort( 0x2ce ),	/* Offset= 718 (1126) */
/* 410 */	NdrFcLong( 0x4003 ),	/* 16387 */
/* 414 */	NdrFcShort( 0x2cc ),	/* Offset= 716 (1130) */
/* 416 */	NdrFcLong( 0x4014 ),	/* 16404 */
/* 420 */	NdrFcShort( 0x2ca ),	/* Offset= 714 (1134) */
/* 422 */	NdrFcLong( 0x4004 ),	/* 16388 */
/* 426 */	NdrFcShort( 0x2c8 ),	/* Offset= 712 (1138) */
/* 428 */	NdrFcLong( 0x4005 ),	/* 16389 */
/* 432 */	NdrFcShort( 0x2c6 ),	/* Offset= 710 (1142) */
/* 434 */	NdrFcLong( 0x400b ),	/* 16395 */
/* 438 */	NdrFcShort( 0x2b0 ),	/* Offset= 688 (1126) */
/* 440 */	NdrFcLong( 0x400a ),	/* 16394 */
/* 444 */	NdrFcShort( 0x2ae ),	/* Offset= 686 (1130) */
/* 446 */	NdrFcLong( 0x4006 ),	/* 16390 */
/* 450 */	NdrFcShort( 0x2b8 ),	/* Offset= 696 (1146) */
/* 452 */	NdrFcLong( 0x4007 ),	/* 16391 */
/* 456 */	NdrFcShort( 0x2ae ),	/* Offset= 686 (1142) */
/* 458 */	NdrFcLong( 0x4008 ),	/* 16392 */
/* 462 */	NdrFcShort( 0x2b0 ),	/* Offset= 688 (1150) */
/* 464 */	NdrFcLong( 0x400d ),	/* 16397 */
/* 468 */	NdrFcShort( 0x2ae ),	/* Offset= 686 (1154) */
/* 470 */	NdrFcLong( 0x4009 ),	/* 16393 */
/* 474 */	NdrFcShort( 0x2ac ),	/* Offset= 684 (1158) */
/* 476 */	NdrFcLong( 0x6000 ),	/* 24576 */
/* 480 */	NdrFcShort( 0x2aa ),	/* Offset= 682 (1162) */
/* 482 */	NdrFcLong( 0x400c ),	/* 16396 */
/* 486 */	NdrFcShort( 0x2a8 ),	/* Offset= 680 (1166) */
/* 488 */	NdrFcLong( 0x10 ),	/* 16 */
/* 492 */	NdrFcShort( 0x8002 ),	/* Simple arm type: FC_CHAR */
/* 494 */	NdrFcLong( 0x12 ),	/* 18 */
/* 498 */	NdrFcShort( 0x8006 ),	/* Simple arm type: FC_SHORT */
/* 500 */	NdrFcLong( 0x13 ),	/* 19 */
/* 504 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 506 */	NdrFcLong( 0x15 ),	/* 21 */
/* 510 */	NdrFcShort( 0x800b ),	/* Simple arm type: FC_HYPER */
/* 512 */	NdrFcLong( 0x16 ),	/* 22 */
/* 516 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 518 */	NdrFcLong( 0x17 ),	/* 23 */
/* 522 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 524 */	NdrFcLong( 0xe ),	/* 14 */
/* 528 */	NdrFcShort( 0x286 ),	/* Offset= 646 (1174) */
/* 530 */	NdrFcLong( 0x400e ),	/* 16398 */
/* 534 */	NdrFcShort( 0x28a ),	/* Offset= 650 (1184) */
/* 536 */	NdrFcLong( 0x4010 ),	/* 16400 */
/* 540 */	NdrFcShort( 0x288 ),	/* Offset= 648 (1188) */
/* 542 */	NdrFcLong( 0x4012 ),	/* 16402 */
/* 546 */	NdrFcShort( 0x244 ),	/* Offset= 580 (1126) */
/* 548 */	NdrFcLong( 0x4013 ),	/* 16403 */
/* 552 */	NdrFcShort( 0x242 ),	/* Offset= 578 (1130) */
/* 554 */	NdrFcLong( 0x4015 ),	/* 16405 */
/* 558 */	NdrFcShort( 0x240 ),	/* Offset= 576 (1134) */
/* 560 */	NdrFcLong( 0x4016 ),	/* 16406 */
/* 564 */	NdrFcShort( 0x236 ),	/* Offset= 566 (1130) */
/* 566 */	NdrFcLong( 0x4017 ),	/* 16407 */
/* 570 */	NdrFcShort( 0x230 ),	/* Offset= 560 (1130) */
/* 572 */	NdrFcLong( 0x0 ),	/* 0 */
/* 576 */	NdrFcShort( 0x0 ),	/* Offset= 0 (576) */
/* 578 */	NdrFcLong( 0x1 ),	/* 1 */
/* 582 */	NdrFcShort( 0x0 ),	/* Offset= 0 (582) */
/* 584 */	NdrFcShort( 0xffff ),	/* Offset= -1 (583) */
/* 586 */	
			0x15,		/* FC_STRUCT */
			0x7,		/* 7 */
/* 588 */	NdrFcShort( 0x8 ),	/* 8 */
/* 590 */	0xb,		/* FC_HYPER */
			0x5b,		/* FC_END */
/* 592 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 594 */	NdrFcLong( 0x20400 ),	/* 132096 */
/* 598 */	NdrFcShort( 0x0 ),	/* 0 */
/* 600 */	NdrFcShort( 0x0 ),	/* 0 */
/* 602 */	0xc0,		/* 192 */
			0x0,		/* 0 */
/* 604 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 606 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 608 */	0x0,		/* 0 */
			0x46,		/* 70 */
/* 610 */	
			0x13, 0x10,	/* FC_OP [pointer_deref] */
/* 612 */	NdrFcShort( 0x2 ),	/* Offset= 2 (614) */
/* 614 */	
			0x13, 0x0,	/* FC_OP */
/* 616 */	NdrFcShort( 0x1e4 ),	/* Offset= 484 (1100) */
/* 618 */	
			0x2a,		/* FC_ENCAPSULATED_UNION */
			0x89,		/* 137 */
/* 620 */	NdrFcShort( 0x20 ),	/* 32 */
/* 622 */	NdrFcShort( 0xa ),	/* 10 */
/* 624 */	NdrFcLong( 0x8 ),	/* 8 */
/* 628 */	NdrFcShort( 0x50 ),	/* Offset= 80 (708) */
/* 630 */	NdrFcLong( 0xd ),	/* 13 */
/* 634 */	NdrFcShort( 0x70 ),	/* Offset= 112 (746) */
/* 636 */	NdrFcLong( 0x9 ),	/* 9 */
/* 640 */	NdrFcShort( 0x90 ),	/* Offset= 144 (784) */
/* 642 */	NdrFcLong( 0xc ),	/* 12 */
/* 646 */	NdrFcShort( 0xb0 ),	/* Offset= 176 (822) */
/* 648 */	NdrFcLong( 0x24 ),	/* 36 */
/* 652 */	NdrFcShort( 0x102 ),	/* Offset= 258 (910) */
/* 654 */	NdrFcLong( 0x800d ),	/* 32781 */
/* 658 */	NdrFcShort( 0x11e ),	/* Offset= 286 (944) */
/* 660 */	NdrFcLong( 0x10 ),	/* 16 */
/* 664 */	NdrFcShort( 0x138 ),	/* Offset= 312 (976) */
/* 666 */	NdrFcLong( 0x2 ),	/* 2 */
/* 670 */	NdrFcShort( 0x14e ),	/* Offset= 334 (1004) */
/* 672 */	NdrFcLong( 0x3 ),	/* 3 */
/* 676 */	NdrFcShort( 0x164 ),	/* Offset= 356 (1032) */
/* 678 */	NdrFcLong( 0x14 ),	/* 20 */
/* 682 */	NdrFcShort( 0x17a ),	/* Offset= 378 (1060) */
/* 684 */	NdrFcShort( 0xffff ),	/* Offset= -1 (683) */
/* 686 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 688 */	NdrFcShort( 0x0 ),	/* 0 */
/* 690 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 692 */	NdrFcShort( 0x0 ),	/* 0 */
/* 694 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 696 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 700 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 702 */	
			0x13, 0x0,	/* FC_OP */
/* 704 */	NdrFcShort( 0xfd56 ),	/* Offset= -682 (22) */
/* 706 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 708 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 710 */	NdrFcShort( 0x10 ),	/* 16 */
/* 712 */	NdrFcShort( 0x0 ),	/* 0 */
/* 714 */	NdrFcShort( 0x6 ),	/* Offset= 6 (720) */
/* 716 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 718 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 720 */	
			0x11, 0x0,	/* FC_RP */
/* 722 */	NdrFcShort( 0xffdc ),	/* Offset= -36 (686) */
/* 724 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 726 */	NdrFcShort( 0x0 ),	/* 0 */
/* 728 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 730 */	NdrFcShort( 0x0 ),	/* 0 */
/* 732 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 734 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 738 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 740 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 742 */	NdrFcShort( 0xfd4c ),	/* Offset= -692 (50) */
/* 744 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 746 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 748 */	NdrFcShort( 0x10 ),	/* 16 */
/* 750 */	NdrFcShort( 0x0 ),	/* 0 */
/* 752 */	NdrFcShort( 0x6 ),	/* Offset= 6 (758) */
/* 754 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 756 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 758 */	
			0x11, 0x0,	/* FC_RP */
/* 760 */	NdrFcShort( 0xffdc ),	/* Offset= -36 (724) */
/* 762 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 764 */	NdrFcShort( 0x0 ),	/* 0 */
/* 766 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 768 */	NdrFcShort( 0x0 ),	/* 0 */
/* 770 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 772 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 776 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 778 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 780 */	NdrFcShort( 0xff44 ),	/* Offset= -188 (592) */
/* 782 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 784 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 786 */	NdrFcShort( 0x10 ),	/* 16 */
/* 788 */	NdrFcShort( 0x0 ),	/* 0 */
/* 790 */	NdrFcShort( 0x6 ),	/* Offset= 6 (796) */
/* 792 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 794 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 796 */	
			0x11, 0x0,	/* FC_RP */
/* 798 */	NdrFcShort( 0xffdc ),	/* Offset= -36 (762) */
/* 800 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 802 */	NdrFcShort( 0x0 ),	/* 0 */
/* 804 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 806 */	NdrFcShort( 0x0 ),	/* 0 */
/* 808 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 810 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 814 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 816 */	
			0x13, 0x0,	/* FC_OP */
/* 818 */	NdrFcShort( 0x176 ),	/* Offset= 374 (1192) */
/* 820 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 822 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 824 */	NdrFcShort( 0x10 ),	/* 16 */
/* 826 */	NdrFcShort( 0x0 ),	/* 0 */
/* 828 */	NdrFcShort( 0x6 ),	/* Offset= 6 (834) */
/* 830 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 832 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 834 */	
			0x11, 0x0,	/* FC_RP */
/* 836 */	NdrFcShort( 0xffdc ),	/* Offset= -36 (800) */
/* 838 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 840 */	NdrFcLong( 0x2f ),	/* 47 */
/* 844 */	NdrFcShort( 0x0 ),	/* 0 */
/* 846 */	NdrFcShort( 0x0 ),	/* 0 */
/* 848 */	0xc0,		/* 192 */
			0x0,		/* 0 */
/* 850 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 852 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 854 */	0x0,		/* 0 */
			0x46,		/* 70 */
/* 856 */	
			0x1b,		/* FC_CARRAY */
			0x0,		/* 0 */
/* 858 */	NdrFcShort( 0x1 ),	/* 1 */
/* 860 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 862 */	NdrFcShort( 0x4 ),	/* 4 */
/* 864 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 866 */	0x1,		/* FC_BYTE */
			0x5b,		/* FC_END */
/* 868 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 870 */	NdrFcShort( 0x18 ),	/* 24 */
/* 872 */	NdrFcShort( 0x0 ),	/* 0 */
/* 874 */	NdrFcShort( 0xa ),	/* Offset= 10 (884) */
/* 876 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 878 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 880 */	NdrFcShort( 0xffd6 ),	/* Offset= -42 (838) */
/* 882 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 884 */	
			0x13, 0x0,	/* FC_OP */
/* 886 */	NdrFcShort( 0xffe2 ),	/* Offset= -30 (856) */
/* 888 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 890 */	NdrFcShort( 0x0 ),	/* 0 */
/* 892 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 894 */	NdrFcShort( 0x0 ),	/* 0 */
/* 896 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 898 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 902 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 904 */	
			0x13, 0x0,	/* FC_OP */
/* 906 */	NdrFcShort( 0xffda ),	/* Offset= -38 (868) */
/* 908 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 910 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 912 */	NdrFcShort( 0x10 ),	/* 16 */
/* 914 */	NdrFcShort( 0x0 ),	/* 0 */
/* 916 */	NdrFcShort( 0x6 ),	/* Offset= 6 (922) */
/* 918 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 920 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 922 */	
			0x11, 0x0,	/* FC_RP */
/* 924 */	NdrFcShort( 0xffdc ),	/* Offset= -36 (888) */
/* 926 */	
			0x1d,		/* FC_SMFARRAY */
			0x0,		/* 0 */
/* 928 */	NdrFcShort( 0x8 ),	/* 8 */
/* 930 */	0x1,		/* FC_BYTE */
			0x5b,		/* FC_END */
/* 932 */	
			0x15,		/* FC_STRUCT */
			0x3,		/* 3 */
/* 934 */	NdrFcShort( 0x10 ),	/* 16 */
/* 936 */	0x8,		/* FC_LONG */
			0x6,		/* FC_SHORT */
/* 938 */	0x6,		/* FC_SHORT */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 940 */	0x0,		/* 0 */
			NdrFcShort( 0xfff1 ),	/* Offset= -15 (926) */
			0x5b,		/* FC_END */
/* 944 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 946 */	NdrFcShort( 0x20 ),	/* 32 */
/* 948 */	NdrFcShort( 0x0 ),	/* 0 */
/* 950 */	NdrFcShort( 0xa ),	/* Offset= 10 (960) */
/* 952 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 954 */	0x36,		/* FC_POINTER */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 956 */	0x0,		/* 0 */
			NdrFcShort( 0xffe7 ),	/* Offset= -25 (932) */
			0x5b,		/* FC_END */
/* 960 */	
			0x11, 0x0,	/* FC_RP */
/* 962 */	NdrFcShort( 0xff12 ),	/* Offset= -238 (724) */
/* 964 */	
			0x1b,		/* FC_CARRAY */
			0x0,		/* 0 */
/* 966 */	NdrFcShort( 0x1 ),	/* 1 */
/* 968 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 970 */	NdrFcShort( 0x0 ),	/* 0 */
/* 972 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 974 */	0x1,		/* FC_BYTE */
			0x5b,		/* FC_END */
/* 976 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 978 */	NdrFcShort( 0x10 ),	/* 16 */
/* 980 */	NdrFcShort( 0x0 ),	/* 0 */
/* 982 */	NdrFcShort( 0x6 ),	/* Offset= 6 (988) */
/* 984 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 986 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 988 */	
			0x13, 0x0,	/* FC_OP */
/* 990 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (964) */
/* 992 */	
			0x1b,		/* FC_CARRAY */
			0x1,		/* 1 */
/* 994 */	NdrFcShort( 0x2 ),	/* 2 */
/* 996 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 998 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1000 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 1002 */	0x6,		/* FC_SHORT */
			0x5b,		/* FC_END */
/* 1004 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 1006 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1008 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1010 */	NdrFcShort( 0x6 ),	/* Offset= 6 (1016) */
/* 1012 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 1014 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 1016 */	
			0x13, 0x0,	/* FC_OP */
/* 1018 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (992) */
/* 1020 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 1022 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1024 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 1026 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1028 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 1030 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 1032 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 1034 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1036 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1038 */	NdrFcShort( 0x6 ),	/* Offset= 6 (1044) */
/* 1040 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 1042 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 1044 */	
			0x13, 0x0,	/* FC_OP */
/* 1046 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (1020) */
/* 1048 */	
			0x1b,		/* FC_CARRAY */
			0x7,		/* 7 */
/* 1050 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1052 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 1054 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1056 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 1058 */	0xb,		/* FC_HYPER */
			0x5b,		/* FC_END */
/* 1060 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 1062 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1064 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1066 */	NdrFcShort( 0x6 ),	/* Offset= 6 (1072) */
/* 1068 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 1070 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 1072 */	
			0x13, 0x0,	/* FC_OP */
/* 1074 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (1048) */
/* 1076 */	
			0x15,		/* FC_STRUCT */
			0x3,		/* 3 */
/* 1078 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1080 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 1082 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1084 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 1086 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1088 */	0x7,		/* Corr desc: FC_USHORT */
			0x0,		/*  */
/* 1090 */	NdrFcShort( 0xffc8 ),	/* -56 */
/* 1092 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 1094 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 1096 */	NdrFcShort( 0xffec ),	/* Offset= -20 (1076) */
/* 1098 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1100 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 1102 */	NdrFcShort( 0x38 ),	/* 56 */
/* 1104 */	NdrFcShort( 0xffec ),	/* Offset= -20 (1084) */
/* 1106 */	NdrFcShort( 0x0 ),	/* Offset= 0 (1106) */
/* 1108 */	0x6,		/* FC_SHORT */
			0x6,		/* FC_SHORT */
/* 1110 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 1112 */	0x40,		/* FC_STRUCTPAD4 */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 1114 */	0x0,		/* 0 */
			NdrFcShort( 0xfe0f ),	/* Offset= -497 (618) */
			0x5b,		/* FC_END */
/* 1118 */	
			0x13, 0x0,	/* FC_OP */
/* 1120 */	NdrFcShort( 0xff04 ),	/* Offset= -252 (868) */
/* 1122 */	
			0x13, 0x8,	/* FC_OP [simple_pointer] */
/* 1124 */	0x1,		/* FC_BYTE */
			0x5c,		/* FC_PAD */
/* 1126 */	
			0x13, 0x8,	/* FC_OP [simple_pointer] */
/* 1128 */	0x6,		/* FC_SHORT */
			0x5c,		/* FC_PAD */
/* 1130 */	
			0x13, 0x8,	/* FC_OP [simple_pointer] */
/* 1132 */	0x8,		/* FC_LONG */
			0x5c,		/* FC_PAD */
/* 1134 */	
			0x13, 0x8,	/* FC_OP [simple_pointer] */
/* 1136 */	0xb,		/* FC_HYPER */
			0x5c,		/* FC_PAD */
/* 1138 */	
			0x13, 0x8,	/* FC_OP [simple_pointer] */
/* 1140 */	0xa,		/* FC_FLOAT */
			0x5c,		/* FC_PAD */
/* 1142 */	
			0x13, 0x8,	/* FC_OP [simple_pointer] */
/* 1144 */	0xc,		/* FC_DOUBLE */
			0x5c,		/* FC_PAD */
/* 1146 */	
			0x13, 0x0,	/* FC_OP */
/* 1148 */	NdrFcShort( 0xfdce ),	/* Offset= -562 (586) */
/* 1150 */	
			0x13, 0x10,	/* FC_OP [pointer_deref] */
/* 1152 */	NdrFcShort( 0xfb86 ),	/* Offset= -1146 (6) */
/* 1154 */	
			0x13, 0x10,	/* FC_OP [pointer_deref] */
/* 1156 */	NdrFcShort( 0xfbae ),	/* Offset= -1106 (50) */
/* 1158 */	
			0x13, 0x10,	/* FC_OP [pointer_deref] */
/* 1160 */	NdrFcShort( 0xfdc8 ),	/* Offset= -568 (592) */
/* 1162 */	
			0x13, 0x10,	/* FC_OP [pointer_deref] */
/* 1164 */	NdrFcShort( 0xfdd6 ),	/* Offset= -554 (610) */
/* 1166 */	
			0x13, 0x10,	/* FC_OP [pointer_deref] */
/* 1168 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1170) */
/* 1170 */	
			0x13, 0x0,	/* FC_OP */
/* 1172 */	NdrFcShort( 0x14 ),	/* Offset= 20 (1192) */
/* 1174 */	
			0x15,		/* FC_STRUCT */
			0x7,		/* 7 */
/* 1176 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1178 */	0x6,		/* FC_SHORT */
			0x1,		/* FC_BYTE */
/* 1180 */	0x1,		/* FC_BYTE */
			0x8,		/* FC_LONG */
/* 1182 */	0xb,		/* FC_HYPER */
			0x5b,		/* FC_END */
/* 1184 */	
			0x13, 0x0,	/* FC_OP */
/* 1186 */	NdrFcShort( 0xfff4 ),	/* Offset= -12 (1174) */
/* 1188 */	
			0x13, 0x8,	/* FC_OP [simple_pointer] */
/* 1190 */	0x2,		/* FC_CHAR */
			0x5c,		/* FC_PAD */
/* 1192 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x7,		/* 7 */
/* 1194 */	NdrFcShort( 0x20 ),	/* 32 */
/* 1196 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1198 */	NdrFcShort( 0x0 ),	/* Offset= 0 (1198) */
/* 1200 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 1202 */	0x6,		/* FC_SHORT */
			0x6,		/* FC_SHORT */
/* 1204 */	0x6,		/* FC_SHORT */
			0x6,		/* FC_SHORT */
/* 1206 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 1208 */	NdrFcShort( 0xfc68 ),	/* Offset= -920 (288) */
/* 1210 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1212 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 1214 */	NdrFcShort( 0x2 ),	/* 2 */
/* 1216 */	NdrFcShort( 0x18 ),	/* 24 */
/* 1218 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1220 */	NdrFcShort( 0xfc58 ),	/* Offset= -936 (284) */
/* 1222 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/* 1224 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1226) */
/* 1226 */	
			0x13, 0x0,	/* FC_OP */
/* 1228 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1230) */
/* 1230 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 1232 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1234 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 1236 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 1238 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1240 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 1244 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1246 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 1248 */	NdrFcShort( 0xfb52 ),	/* Offset= -1198 (50) */
/* 1250 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1252 */	
			0x11, 0x0,	/* FC_RP */
/* 1254 */	NdrFcShort( 0x6 ),	/* Offset= 6 (1260) */
/* 1256 */	
			0x12, 0x0,	/* FC_UP */
/* 1258 */	NdrFcShort( 0xffbe ),	/* Offset= -66 (1192) */
/* 1260 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 1262 */	NdrFcShort( 0x2 ),	/* 2 */
/* 1264 */	NdrFcShort( 0x18 ),	/* 24 */
/* 1266 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1268 */	NdrFcShort( 0xfff4 ),	/* Offset= -12 (1256) */
/* 1270 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 1272 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1274) */
/* 1274 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 1276 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1278 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1280 */	NdrFcShort( 0x0 ),	/* Offset= 0 (1280) */
/* 1282 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 1284 */	NdrFcShort( 0xfb1c ),	/* Offset= -1252 (32) */
/* 1286 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 1288 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1290 */	
			0x11, 0x0,	/* FC_RP */
/* 1292 */	NdrFcShort( 0xfc02 ),	/* Offset= -1022 (270) */
/* 1294 */	
			0x11, 0xc,	/* FC_RP [alloced_on_stack] [simple_pointer] */
/* 1296 */	0x3,		/* FC_SMALL */
			0x5c,		/* FC_PAD */
/* 1298 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1300 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1302) */
/* 1302 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1304 */	NdrFcLong( 0x1c20f2b ),	/* 29495083 */
/* 1308 */	NdrFcShort( 0x3dd2 ),	/* 15826 */
/* 1310 */	NdrFcShort( 0x400f ),	/* 16399 */
/* 1312 */	0x94,		/* 148 */
			0x9f,		/* 159 */
/* 1314 */	0xad,		/* 173 */
			0x0,		/* 0 */
/* 1316 */	0xbd,		/* 189 */
			0xab,		/* 171 */
/* 1318 */	0x1d,		/* 29 */
			0x41,		/* 65 */
/* 1320 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/* 1322 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1324) */
/* 1324 */	
			0x13, 0x0,	/* FC_OP */
/* 1326 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1328) */
/* 1328 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 1330 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1332 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 1334 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1336 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1338 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 1342 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1344 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 1346 */	NdrFcShort( 0xffd4 ),	/* Offset= -44 (1302) */
/* 1348 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1350 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1352 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1354) */
/* 1354 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1356 */	NdrFcLong( 0x35ad8070 ),	/* 900563056 */
/* 1360 */	NdrFcShort( 0xc20c ),	/* -15860 */
/* 1362 */	NdrFcShort( 0x4fb4 ),	/* 20404 */
/* 1364 */	0xb0,		/* 176 */
			0x94,		/* 148 */
/* 1366 */	0xf4,		/* 244 */
			0xf7,		/* 247 */
/* 1368 */	0x27,		/* 39 */
			0x5d,		/* 93 */
/* 1370 */	0xd4,		/* 212 */
			0x69,		/* 105 */
/* 1372 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/* 1374 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1376) */
/* 1376 */	
			0x13, 0x0,	/* FC_OP */
/* 1378 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1380) */
/* 1380 */	
			0x1c,		/* FC_CVARRAY */
			0x3,		/* 3 */
/* 1382 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1384 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x0,		/*  */
/* 1386 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 1388 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 1390 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 1392 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 1394 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1396 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 1398 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 1400 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1402) */
/* 1402 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 1404 */	NdrFcShort( 0x14 ),	/* 20 */
/* 1406 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1408 */	NdrFcShort( 0x0 ),	/* Offset= 0 (1408) */
/* 1410 */	0xd,		/* FC_ENUM16 */
			0x8,		/* FC_LONG */
/* 1412 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 1414 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 1416 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/* 1418 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1420) */
/* 1420 */	
			0x13, 0x0,	/* FC_OP */
/* 1422 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1424) */
/* 1424 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 1426 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1428 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 1430 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1432 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1434 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 1438 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1440 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 1442 */	NdrFcShort( 0xfa90 ),	/* Offset= -1392 (50) */
/* 1444 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1446 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/* 1448 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1450) */
/* 1450 */	
			0x13, 0x0,	/* FC_OP */
/* 1452 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1454) */
/* 1454 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 1456 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1458 */	0x28,		/* Corr desc:  parameter, FC_LONG */
			0x54,		/* FC_DEREFERENCE */
/* 1460 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 1462 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 1464 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */

			0x0
        }
    };

static const USER_MARSHAL_ROUTINE_QUADRUPLE UserMarshalRoutines[ WIRE_MARSHAL_TABLE_SIZE ] = 
        {
            
            {
            BSTR_UserSize
            ,BSTR_UserMarshal
            ,BSTR_UserUnmarshal
            ,BSTR_UserFree
            },
            {
            HWND_UserSize
            ,HWND_UserMarshal
            ,HWND_UserUnmarshal
            ,HWND_UserFree
            },
            {
            VARIANT_UserSize
            ,VARIANT_UserMarshal
            ,VARIANT_UserUnmarshal
            ,VARIANT_UserFree
            }

        };



/* Standard interface: __MIDL_itf_ia2_api_all_0000_0000, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IUnknown, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: IAccessibleRelation, ver. 0.0,
   GUID={0x7CDF86EE,0xC3DA,0x496a,{0xBD,0xA4,0x28,0x1B,0x33,0x6E,0x1F,0xDC}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleRelation_FormatStringOffsetTable[] =
    {
    0,
    38,
    76,
    114,
    158
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleRelation_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleRelation_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleRelation_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleRelation_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(8) _IAccessibleRelationProxyVtbl = 
{
    &IAccessibleRelation_ProxyInfo,
    &IID_IAccessibleRelation,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleRelation::get_relationType */ ,
    (void *) (INT_PTR) -1 /* IAccessibleRelation::get_localizedRelationType */ ,
    (void *) (INT_PTR) -1 /* IAccessibleRelation::get_nTargets */ ,
    (void *) (INT_PTR) -1 /* IAccessibleRelation::get_target */ ,
    (void *) (INT_PTR) -1 /* IAccessibleRelation::get_targets */
};

const CInterfaceStubVtbl _IAccessibleRelationStubVtbl =
{
    &IID_IAccessibleRelation,
    &IAccessibleRelation_ServerInfo,
    8,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Standard interface: __MIDL_itf_ia2_api_all_0000_0001, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IAccessibleAction, ver. 0.0,
   GUID={0xB70D9F59,0x3B5A,0x4dba,{0xAB,0x9E,0x22,0x01,0x2F,0x60,0x7D,0xF5}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleAction_FormatStringOffsetTable[] =
    {
    208,
    246,
    284,
    328,
    384,
    428
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleAction_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleAction_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleAction_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleAction_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(9) _IAccessibleActionProxyVtbl = 
{
    &IAccessibleAction_ProxyInfo,
    &IID_IAccessibleAction,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::nActions */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::doAction */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_description */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_keyBinding */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_name */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_localizedName */
};

const CInterfaceStubVtbl _IAccessibleActionStubVtbl =
{
    &IID_IAccessibleAction,
    &IAccessibleAction_ServerInfo,
    9,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Standard interface: __MIDL_itf_ia2_api_all_0000_0002, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IDispatch, ver. 0.0,
   GUID={0x00020400,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: IAccessible, ver. 0.0,
   GUID={0x618736e0,0x3c3d,0x11cf,{0x81,0x0c,0x00,0xaa,0x00,0x38,0x9b,0x71}} */


/* Object interface: IAccessible2, ver. 0.0,
   GUID={0xE89F726E,0xC4F4,0x4c19,{0xBB,0x19,0xB6,0x47,0xD7,0xFA,0x84,0x78}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessible2_FormatStringOffsetTable[] =
    {
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    472,
    510,
    554,
    604,
    642,
    680,
    730,
    780,
    818,
    856,
    894,
    932,
    982,
    1032,
    1070,
    1108,
    1146,
    1184
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessible2_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessible2_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessible2_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessible2_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(46) _IAccessible2ProxyVtbl = 
{
    &IAccessible2_ProxyInfo,
    &IID_IAccessible2,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    0 /* IDispatch::GetTypeInfoCount */ ,
    0 /* IDispatch::GetTypeInfo */ ,
    0 /* IDispatch::GetIDsOfNames */ ,
    0 /* IDispatch_Invoke_Proxy */ ,
    0 /* IAccessible::get_accParent */ ,
    0 /* IAccessible::get_accChildCount */ ,
    0 /* IAccessible::get_accChild */ ,
    0 /* IAccessible::get_accName */ ,
    0 /* IAccessible::get_accValue */ ,
    0 /* IAccessible::get_accDescription */ ,
    0 /* IAccessible::get_accRole */ ,
    0 /* IAccessible::get_accState */ ,
    0 /* IAccessible::get_accHelp */ ,
    0 /* IAccessible::get_accHelpTopic */ ,
    0 /* IAccessible::get_accKeyboardShortcut */ ,
    0 /* IAccessible::get_accFocus */ ,
    0 /* IAccessible::get_accSelection */ ,
    0 /* IAccessible::get_accDefaultAction */ ,
    0 /* IAccessible::accSelect */ ,
    0 /* IAccessible::accLocation */ ,
    0 /* IAccessible::accNavigate */ ,
    0 /* IAccessible::accHitTest */ ,
    0 /* IAccessible::accDoDefaultAction */ ,
    0 /* IAccessible::put_accName */ ,
    0 /* IAccessible::put_accValue */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_nRelations */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_relation */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_relations */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::role */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::scrollTo */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::scrollToPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_groupPosition */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_states */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_extendedRole */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_localizedExtendedRole */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_nExtendedStates */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_extendedStates */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_localizedExtendedStates */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_uniqueID */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_windowHandle */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_indexInParent */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_locale */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_attributes */
};


static const PRPC_STUB_FUNCTION IAccessible2_table[] =
{
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2
};

CInterfaceStubVtbl _IAccessible2StubVtbl =
{
    &IID_IAccessible2,
    &IAccessible2_ServerInfo,
    46,
    &IAccessible2_table[-3],
    CStdStubBuffer_DELEGATING_METHODS
};


/* Object interface: IAccessible2_2, ver. 0.0,
   GUID={0x6C9430E9,0x299D,0x4E6F,{0xBD,0x01,0xA8,0x2A,0x1E,0x88,0xD3,0xFF}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessible2_2_FormatStringOffsetTable[] =
    {
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    (unsigned short) -1,
    472,
    510,
    554,
    604,
    642,
    680,
    730,
    780,
    818,
    856,
    894,
    932,
    982,
    1032,
    1070,
    1108,
    1146,
    1184,
    1222,
    1266,
    1310
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessible2_2_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessible2_2_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessible2_2_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessible2_2_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(49) _IAccessible2_2ProxyVtbl = 
{
    &IAccessible2_2_ProxyInfo,
    &IID_IAccessible2_2,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    0 /* IDispatch::GetTypeInfoCount */ ,
    0 /* IDispatch::GetTypeInfo */ ,
    0 /* IDispatch::GetIDsOfNames */ ,
    0 /* IDispatch_Invoke_Proxy */ ,
    0 /* IAccessible::get_accParent */ ,
    0 /* IAccessible::get_accChildCount */ ,
    0 /* IAccessible::get_accChild */ ,
    0 /* IAccessible::get_accName */ ,
    0 /* IAccessible::get_accValue */ ,
    0 /* IAccessible::get_accDescription */ ,
    0 /* IAccessible::get_accRole */ ,
    0 /* IAccessible::get_accState */ ,
    0 /* IAccessible::get_accHelp */ ,
    0 /* IAccessible::get_accHelpTopic */ ,
    0 /* IAccessible::get_accKeyboardShortcut */ ,
    0 /* IAccessible::get_accFocus */ ,
    0 /* IAccessible::get_accSelection */ ,
    0 /* IAccessible::get_accDefaultAction */ ,
    0 /* IAccessible::accSelect */ ,
    0 /* IAccessible::accLocation */ ,
    0 /* IAccessible::accNavigate */ ,
    0 /* IAccessible::accHitTest */ ,
    0 /* IAccessible::accDoDefaultAction */ ,
    0 /* IAccessible::put_accName */ ,
    0 /* IAccessible::put_accValue */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_nRelations */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_relation */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_relations */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::role */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::scrollTo */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::scrollToPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_groupPosition */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_states */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_extendedRole */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_localizedExtendedRole */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_nExtendedStates */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_extendedStates */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_localizedExtendedStates */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_uniqueID */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_windowHandle */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_indexInParent */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_locale */ ,
    (void *) (INT_PTR) -1 /* IAccessible2::get_attributes */ ,
    (void *) (INT_PTR) -1 /* IAccessible2_2::get_attribute */ ,
    (void *) (INT_PTR) -1 /* IAccessible2_2::get_accessibleWithCaret */ ,
    (void *) (INT_PTR) -1 /* IAccessible2_2::get_relationTargetsOfType */
};


static const PRPC_STUB_FUNCTION IAccessible2_2_table[] =
{
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    STUB_FORWARDING_FUNCTION,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2,
    NdrStubCall2
};

CInterfaceStubVtbl _IAccessible2_2StubVtbl =
{
    &IID_IAccessible2_2,
    &IAccessible2_2_ServerInfo,
    49,
    &IAccessible2_2_table[-3],
    CStdStubBuffer_DELEGATING_METHODS
};


/* Standard interface: __MIDL_itf_ia2_api_all_0000_0004, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IAccessibleComponent, ver. 0.0,
   GUID={0x1546D4B0,0x4C98,0x4bda,{0x89,0xAE,0x9A,0x64,0x74,0x8B,0xDD,0xE4}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleComponent_FormatStringOffsetTable[] =
    {
    1366,
    1410,
    76
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleComponent_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleComponent_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleComponent_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleComponent_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(6) _IAccessibleComponentProxyVtbl = 
{
    &IAccessibleComponent_ProxyInfo,
    &IID_IAccessibleComponent,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleComponent::get_locationInParent */ ,
    (void *) (INT_PTR) -1 /* IAccessibleComponent::get_foreground */ ,
    (void *) (INT_PTR) -1 /* IAccessibleComponent::get_background */
};

const CInterfaceStubVtbl _IAccessibleComponentStubVtbl =
{
    &IID_IAccessibleComponent,
    &IAccessibleComponent_ServerInfo,
    6,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleValue, ver. 0.0,
   GUID={0x35855B5B,0xC566,0x4fd0,{0xA7,0xB1,0xE6,0x54,0x65,0x60,0x03,0x94}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleValue_FormatStringOffsetTable[] =
    {
    1448,
    1486,
    1524,
    1562
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleValue_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleValue_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleValue_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleValue_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(7) _IAccessibleValueProxyVtbl = 
{
    &IAccessibleValue_ProxyInfo,
    &IID_IAccessibleValue,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleValue::get_currentValue */ ,
    (void *) (INT_PTR) -1 /* IAccessibleValue::setCurrentValue */ ,
    (void *) (INT_PTR) -1 /* IAccessibleValue::get_maximumValue */ ,
    (void *) (INT_PTR) -1 /* IAccessibleValue::get_minimumValue */
};

const CInterfaceStubVtbl _IAccessibleValueStubVtbl =
{
    &IID_IAccessibleValue,
    &IAccessibleValue_ServerInfo,
    7,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Standard interface: __MIDL_itf_ia2_api_all_0000_0006, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IAccessibleText, ver. 0.0,
   GUID={0x24FD2FFB,0x3AAD,0x4a08,{0x83,0x35,0xA3,0xAD,0x89,0xC0,0xFB,0x4B}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleText_FormatStringOffsetTable[] =
    {
    1600,
    1644,
    76,
    1700,
    1768,
    1806,
    1862,
    1912,
    1962,
    2024,
    2086,
    2148,
    2186,
    2224,
    2274,
    2312,
    2362,
    2424,
    2462
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleText_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleText_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleText_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleText_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(22) _IAccessibleTextProxyVtbl = 
{
    &IAccessibleText_ProxyInfo,
    &IID_IAccessibleText,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleText::addSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_attributes */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_caretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_characterExtents */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nSelections */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_offsetAtPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_selection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_text */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textBeforeOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAfterOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAtOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::removeSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setCaretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nCharacters */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringTo */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringToPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_newText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_oldText */
};

const CInterfaceStubVtbl _IAccessibleTextStubVtbl =
{
    &IID_IAccessibleText,
    &IAccessibleText_ServerInfo,
    22,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleText2, ver. 0.0,
   GUID={0x9690A9CC,0x5C80,0x4DF5,{0x85,0x2E,0x2D,0x5A,0xE4,0x18,0x9A,0x54}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleText2_FormatStringOffsetTable[] =
    {
    1600,
    1644,
    76,
    1700,
    1768,
    1806,
    1862,
    1912,
    1962,
    2024,
    2086,
    2148,
    2186,
    2224,
    2274,
    2312,
    2362,
    2424,
    2462,
    2500
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleText2_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleText2_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleText2_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleText2_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(23) _IAccessibleText2ProxyVtbl = 
{
    &IAccessibleText2_ProxyInfo,
    &IID_IAccessibleText2,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleText::addSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_attributes */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_caretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_characterExtents */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nSelections */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_offsetAtPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_selection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_text */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textBeforeOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAfterOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAtOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::removeSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setCaretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nCharacters */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringTo */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringToPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_newText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_oldText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText2::get_attributeRange */
};

const CInterfaceStubVtbl _IAccessibleText2StubVtbl =
{
    &IID_IAccessibleText2,
    &IAccessibleText2_ServerInfo,
    23,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleEditableText, ver. 0.0,
   GUID={0xA59AA09A,0x7011,0x4b65,{0x93,0x9D,0x32,0xB1,0xFB,0x55,0x47,0xE3}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleEditableText_FormatStringOffsetTable[] =
    {
    1600,
    2562,
    2606,
    2650,
    2694,
    2732,
    2782
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleEditableText_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleEditableText_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleEditableText_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleEditableText_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(10) _IAccessibleEditableTextProxyVtbl = 
{
    &IAccessibleEditableText_ProxyInfo,
    &IID_IAccessibleEditableText,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleEditableText::copyText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleEditableText::deleteText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleEditableText::insertText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleEditableText::cutText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleEditableText::pasteText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleEditableText::replaceText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleEditableText::setAttributes */
};

const CInterfaceStubVtbl _IAccessibleEditableTextStubVtbl =
{
    &IID_IAccessibleEditableText,
    &IAccessibleEditableText_ServerInfo,
    10,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleHyperlink, ver. 0.0,
   GUID={0x01C20F2B,0x3DD2,0x400f,{0x94,0x9F,0xAD,0x00,0xBD,0xAB,0x1D,0x41}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleHyperlink_FormatStringOffsetTable[] =
    {
    208,
    246,
    284,
    328,
    384,
    428,
    2832,
    2876,
    2920,
    2958,
    2996
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleHyperlink_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleHyperlink_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleHyperlink_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleHyperlink_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(14) _IAccessibleHyperlinkProxyVtbl = 
{
    &IAccessibleHyperlink_ProxyInfo,
    &IID_IAccessibleHyperlink,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::nActions */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::doAction */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_description */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_keyBinding */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_name */ ,
    (void *) (INT_PTR) -1 /* IAccessibleAction::get_localizedName */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHyperlink::get_anchor */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHyperlink::get_anchorTarget */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHyperlink::get_startIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHyperlink::get_endIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHyperlink::get_valid */
};

const CInterfaceStubVtbl _IAccessibleHyperlinkStubVtbl =
{
    &IID_IAccessibleHyperlink,
    &IAccessibleHyperlink_ServerInfo,
    14,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleHypertext, ver. 0.0,
   GUID={0x6B4F8BBF,0xF1F2,0x418a,{0xB3,0x5E,0xA1,0x95,0xBC,0x41,0x03,0xB9}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleHypertext_FormatStringOffsetTable[] =
    {
    1600,
    1644,
    76,
    1700,
    1768,
    1806,
    1862,
    1912,
    1962,
    2024,
    2086,
    2148,
    2186,
    2224,
    2274,
    2312,
    2362,
    2424,
    2462,
    3034,
    3072,
    3116
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleHypertext_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleHypertext_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleHypertext_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleHypertext_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(25) _IAccessibleHypertextProxyVtbl = 
{
    &IAccessibleHypertext_ProxyInfo,
    &IID_IAccessibleHypertext,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleText::addSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_attributes */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_caretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_characterExtents */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nSelections */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_offsetAtPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_selection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_text */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textBeforeOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAfterOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAtOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::removeSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setCaretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nCharacters */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringTo */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringToPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_newText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_oldText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHypertext::get_nHyperlinks */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHypertext::get_hyperlink */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHypertext::get_hyperlinkIndex */
};

const CInterfaceStubVtbl _IAccessibleHypertextStubVtbl =
{
    &IID_IAccessibleHypertext,
    &IAccessibleHypertext_ServerInfo,
    25,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleHypertext2, ver. 0.0,
   GUID={0xCF64D89F,0x8287,0x4B44,{0x85,0x01,0xA8,0x27,0x45,0x3A,0x60,0x77}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleHypertext2_FormatStringOffsetTable[] =
    {
    1600,
    1644,
    76,
    1700,
    1768,
    1806,
    1862,
    1912,
    1962,
    2024,
    2086,
    2148,
    2186,
    2224,
    2274,
    2312,
    2362,
    2424,
    2462,
    3034,
    3072,
    3116,
    3160
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleHypertext2_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleHypertext2_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleHypertext2_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleHypertext2_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(26) _IAccessibleHypertext2ProxyVtbl = 
{
    &IAccessibleHypertext2_ProxyInfo,
    &IID_IAccessibleHypertext2,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleText::addSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_attributes */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_caretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_characterExtents */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nSelections */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_offsetAtPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_selection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_text */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textBeforeOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAfterOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_textAtOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::removeSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setCaretOffset */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::setSelection */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_nCharacters */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringTo */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::scrollSubstringToPoint */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_newText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleText::get_oldText */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHypertext::get_nHyperlinks */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHypertext::get_hyperlink */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHypertext::get_hyperlinkIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleHypertext2::get_hyperlinks */
};

const CInterfaceStubVtbl _IAccessibleHypertext2StubVtbl =
{
    &IID_IAccessibleHypertext2,
    &IAccessibleHypertext2_ServerInfo,
    26,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleTable, ver. 0.0,
   GUID={0x35AD8070,0xC20C,0x4fb4,{0xB0,0x94,0xF4,0xF7,0x27,0x5D,0xD4,0x69}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleTable_FormatStringOffsetTable[] =
    {
    3204,
    3254,
    3292,
    3342,
    3386,
    3436,
    3480,
    3524,
    2920,
    2958,
    3562,
    3600,
    3638,
    3682,
    3732,
    3776,
    3820,
    3870,
    3920,
    3970,
    4008,
    4052,
    4096,
    4146,
    4184,
    4222,
    4260,
    4298,
    4366
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleTable_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleTable_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleTable_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleTable_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(32) _IAccessibleTableProxyVtbl = 
{
    &IAccessibleTable_ProxyInfo,
    &IID_IAccessibleTable,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_accessibleAt */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_caption */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_childIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_columnDescription */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_columnExtentAt */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_columnHeader */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_columnIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_nColumns */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_nRows */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_nSelectedChildren */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_nSelectedColumns */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_nSelectedRows */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_rowDescription */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_rowExtentAt */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_rowHeader */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_rowIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_selectedChildren */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_selectedColumns */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_selectedRows */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_summary */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_isColumnSelected */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_isRowSelected */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_isSelected */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::selectRow */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::selectColumn */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::unselectRow */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::unselectColumn */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_rowColumnExtentsAtIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable::get_modelChange */
};

const CInterfaceStubVtbl _IAccessibleTableStubVtbl =
{
    &IID_IAccessibleTable,
    &IAccessibleTable_ServerInfo,
    32,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleTable2, ver. 0.0,
   GUID={0x6167f295,0x06f0,0x4cdd,{0xa1,0xfa,0x02,0xe2,0x51,0x53,0xd8,0x69}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleTable2_FormatStringOffsetTable[] =
    {
    3204,
    3254,
    284,
    4404,
    1768,
    4442,
    4480,
    3524,
    4518,
    4562,
    4606,
    4650,
    4694,
    4732,
    4776,
    4820,
    4858,
    4896,
    4934,
    4972
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleTable2_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleTable2_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleTable2_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleTable2_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(23) _IAccessibleTable2ProxyVtbl = 
{
    &IAccessibleTable2_ProxyInfo,
    &IID_IAccessibleTable2,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_cellAt */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_caption */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_columnDescription */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_nColumns */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_nRows */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_nSelectedCells */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_nSelectedColumns */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_nSelectedRows */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_rowDescription */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_selectedCells */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_selectedColumns */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_selectedRows */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_summary */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_isColumnSelected */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_isRowSelected */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::selectRow */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::selectColumn */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::unselectRow */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::unselectColumn */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTable2::get_modelChange */
};

const CInterfaceStubVtbl _IAccessibleTable2StubVtbl =
{
    &IID_IAccessibleTable2,
    &IAccessibleTable2_ServerInfo,
    23,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleTableCell, ver. 0.0,
   GUID={0x594116B1,0xC99F,0x4847,{0xAD,0x06,0x0A,0x7A,0x86,0xEC,0xE6,0x45}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleTableCell_FormatStringOffsetTable[] =
    {
    208,
    5010,
    76,
    4404,
    5054,
    4442,
    5098,
    5136,
    5198
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleTableCell_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleTableCell_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleTableCell_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleTableCell_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(12) _IAccessibleTableCellProxyVtbl = 
{
    &IAccessibleTableCell_ProxyInfo,
    &IID_IAccessibleTableCell,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_columnExtent */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_columnHeaderCells */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_columnIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_rowExtent */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_rowHeaderCells */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_rowIndex */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_isSelected */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_rowColumnExtents */ ,
    (void *) (INT_PTR) -1 /* IAccessibleTableCell::get_table */
};

const CInterfaceStubVtbl _IAccessibleTableCellStubVtbl =
{
    &IID_IAccessibleTableCell,
    &IAccessibleTableCell_ServerInfo,
    12,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleImage, ver. 0.0,
   GUID={0xFE5ABB3D,0x615E,0x4f7b,{0x90,0x9F,0x5F,0x0E,0xDA,0x9E,0x8D,0xDE}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleImage_FormatStringOffsetTable[] =
    {
    0,
    5236,
    5286
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleImage_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleImage_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleImage_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleImage_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(6) _IAccessibleImageProxyVtbl = 
{
    &IAccessibleImage_ProxyInfo,
    &IID_IAccessibleImage,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleImage::get_description */ ,
    (void *) (INT_PTR) -1 /* IAccessibleImage::get_imagePosition */ ,
    (void *) (INT_PTR) -1 /* IAccessibleImage::get_imageSize */
};

const CInterfaceStubVtbl _IAccessibleImageStubVtbl =
{
    &IID_IAccessibleImage,
    &IAccessibleImage_ServerInfo,
    6,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Standard interface: __MIDL_itf_ia2_api_all_0000_0016, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IAccessibleApplication, ver. 0.0,
   GUID={0xD49DED83,0x5B25,0x43F4,{0x9B,0x95,0x93,0xB4,0x45,0x95,0x97,0x9E}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleApplication_FormatStringOffsetTable[] =
    {
    0,
    38,
    5330,
    5368
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleApplication_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleApplication_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleApplication_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleApplication_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(7) _IAccessibleApplicationProxyVtbl = 
{
    &IAccessibleApplication_ProxyInfo,
    &IID_IAccessibleApplication,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleApplication::get_appName */ ,
    (void *) (INT_PTR) -1 /* IAccessibleApplication::get_appVersion */ ,
    (void *) (INT_PTR) -1 /* IAccessibleApplication::get_toolkitName */ ,
    (void *) (INT_PTR) -1 /* IAccessibleApplication::get_toolkitVersion */
};

const CInterfaceStubVtbl _IAccessibleApplicationStubVtbl =
{
    &IID_IAccessibleApplication,
    &IAccessibleApplication_ServerInfo,
    7,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IAccessibleDocument, ver. 0.0,
   GUID={0xC48C7FCF,0x4AB5,0x4056,{0xAF,0xA6,0x90,0x2D,0x6E,0x1D,0x11,0x49}} */

#pragma code_seg(".orpc")
static const unsigned short IAccessibleDocument_FormatStringOffsetTable[] =
    {
    5406
    };

static const MIDL_STUBLESS_PROXY_INFO IAccessibleDocument_ProxyInfo =
    {
    &Object_StubDesc,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleDocument_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IAccessibleDocument_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    ia2_api_all__MIDL_ProcFormatString.Format,
    &IAccessibleDocument_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(4) _IAccessibleDocumentProxyVtbl = 
{
    &IAccessibleDocument_ProxyInfo,
    &IID_IAccessibleDocument,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IAccessibleDocument::get_anchorTarget */
};

const CInterfaceStubVtbl _IAccessibleDocumentStubVtbl =
{
    &IID_IAccessibleDocument,
    &IAccessibleDocument_ServerInfo,
    4,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Standard interface: __MIDL_itf_ia2_api_all_0000_0018, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */

static const MIDL_STUB_DESC Object_StubDesc = 
    {
    0,
    NdrOleAllocate,
    NdrOleFree,
    0,
    0,
    0,
    0,
    0,
    ia2_api_all__MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0x50002, /* Ndr library version */
    0,
    0x801026e, /* MIDL Version 8.1.622 */
    0,
    UserMarshalRoutines,
    0,  /* notify & notify_flag routine table */
    0x1, /* MIDL flag */
    0, /* cs routines */
    0,   /* proxy/server info */
    0
    };

const CInterfaceProxyVtbl * const _ia2_api_all_ProxyVtblList[] = 
{
    ( CInterfaceProxyVtbl *) &_IAccessibleHyperlinkProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleImageProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleActionProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleValueProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessible2ProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleTableProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleApplicationProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleTable2ProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleEditableTextProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleHypertext2ProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleComponentProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleTableCellProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleHypertextProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleText2ProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleDocumentProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessible2_2ProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleRelationProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IAccessibleTextProxyVtbl,
    0
};

const CInterfaceStubVtbl * const _ia2_api_all_StubVtblList[] = 
{
    ( CInterfaceStubVtbl *) &_IAccessibleHyperlinkStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleImageStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleActionStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleValueStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessible2StubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleTableStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleApplicationStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleTable2StubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleEditableTextStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleHypertext2StubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleComponentStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleTableCellStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleHypertextStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleText2StubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleDocumentStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessible2_2StubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleRelationStubVtbl,
    ( CInterfaceStubVtbl *) &_IAccessibleTextStubVtbl,
    0
};

PCInterfaceName const _ia2_api_all_InterfaceNamesList[] = 
{
    "IAccessibleHyperlink",
    "IAccessibleImage",
    "IAccessibleAction",
    "IAccessibleValue",
    "IAccessible2",
    "IAccessibleTable",
    "IAccessibleApplication",
    "IAccessibleTable2",
    "IAccessibleEditableText",
    "IAccessibleHypertext2",
    "IAccessibleComponent",
    "IAccessibleTableCell",
    "IAccessibleHypertext",
    "IAccessibleText2",
    "IAccessibleDocument",
    "IAccessible2_2",
    "IAccessibleRelation",
    "IAccessibleText",
    0
};

const IID *  const _ia2_api_all_BaseIIDList[] = 
{
    0,
    0,
    0,
    0,
    &IID_IAccessible,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    &IID_IAccessible,
    0,
    0,
    0
};


#define _ia2_api_all_CHECK_IID(n)	IID_GENERIC_CHECK_IID( _ia2_api_all, pIID, n)

int __stdcall _ia2_api_all_IID_Lookup( const IID * pIID, int * pIndex )
{
    IID_BS_LOOKUP_SETUP

    IID_BS_LOOKUP_INITIAL_TEST( _ia2_api_all, 18, 16 )
    IID_BS_LOOKUP_NEXT_TEST( _ia2_api_all, 8 )
    IID_BS_LOOKUP_NEXT_TEST( _ia2_api_all, 4 )
    IID_BS_LOOKUP_NEXT_TEST( _ia2_api_all, 2 )
    IID_BS_LOOKUP_NEXT_TEST( _ia2_api_all, 1 )
    IID_BS_LOOKUP_RETURN_RESULT( _ia2_api_all, 18, *pIndex )
    
}

const ExtendedProxyFileInfo ia2_api_all_ProxyFileInfo = 
{
    (PCInterfaceProxyVtblList *) & _ia2_api_all_ProxyVtblList,
    (PCInterfaceStubVtblList *) & _ia2_api_all_StubVtblList,
    (const PCInterfaceName * ) & _ia2_api_all_InterfaceNamesList,
    (const IID ** ) & _ia2_api_all_BaseIIDList,
    & _ia2_api_all_IID_Lookup, 
    18,
    2,
    0, /* table of [async_uuid] interfaces */
    0, /* Filler1 */
    0, /* Filler2 */
    0  /* Filler3 */
};
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* defined(_M_AMD64)*/

