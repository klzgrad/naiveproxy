

/* this ALWAYS GENERATED file contains the proxy stub code */


 /* File created by MIDL compiler version 8.xx.xxxx */
/* at a redacted point in time
 */
/* Compiler settings for gen/chrome/credential_provider/gaiacp/gaia_credential_provider.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.xx.xxxx 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if !defined(_M_IA64) && !defined(_M_AMD64) && !defined(_ARM_)


#pragma warning( disable: 4049 )  /* more than 64k source lines */
#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/
#pragma warning( disable: 4152 )  /* function/data pointer conversion in expression */
#pragma warning( disable: 4100 ) /* unreferenced arguments in x86 call */

#pragma optimize("", off ) 

#define USE_STUBLESS_PROXY


/* verify that the <rpcproxy.h> version is high enough to compile this file*/
#ifndef __REDQ_RPCPROXY_H_VERSION__
#define __REQUIRED_RPCPROXY_H_VERSION__ 475
#endif


#include "rpcproxy.h"
#ifndef __RPCPROXY_H_VERSION__
#error this stub requires an updated version of <rpcproxy.h>
#endif /* __RPCPROXY_H_VERSION__ */


#include "gaia_credential_provider_i.h"

#define TYPE_FORMAT_STRING_SIZE   93                                
#define PROC_FORMAT_STRING_SIZE   391                               
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   1            

typedef struct _gaia_credential_provider_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } gaia_credential_provider_MIDL_TYPE_FORMAT_STRING;

typedef struct _gaia_credential_provider_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } gaia_credential_provider_MIDL_PROC_FORMAT_STRING;

typedef struct _gaia_credential_provider_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } gaia_credential_provider_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax = 
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};


extern const gaia_credential_provider_MIDL_TYPE_FORMAT_STRING gaia_credential_provider__MIDL_TypeFormatString;
extern const gaia_credential_provider_MIDL_PROC_FORMAT_STRING gaia_credential_provider__MIDL_ProcFormatString;
extern const gaia_credential_provider_MIDL_EXPR_FORMAT_STRING gaia_credential_provider__MIDL_ExprFormatString;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IGaiaCredentialProvider_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IGaiaCredentialProvider_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IGaiaCredentialProviderForTesting_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IGaiaCredentialProviderForTesting_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IGaiaCredential_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IGaiaCredential_ProxyInfo;


extern const MIDL_STUB_DESC Object_StubDesc;


extern const MIDL_SERVER_INFO IReauthCredential_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IReauthCredential_ProxyInfo;


extern const USER_MARSHAL_ROUTINE_QUADRUPLE UserMarshalRoutines[ WIRE_MARSHAL_TABLE_SIZE ];

#if !defined(__RPC_WIN32__)
#error  Invalid build platform for this stub.
#endif

#if !(TARGET_IS_NT50_OR_LATER)
#error You need Windows 2000 or later to run this stub because it uses these features:
#error   /robust command line switch.
#error However, your C/C++ compilation flags indicate you intend to run this app on earlier systems.
#error This app will fail with the RPC_X_WRONG_STUB_VERSION error.
#endif


static const gaia_credential_provider_MIDL_PROC_FORMAT_STRING gaia_credential_provider__MIDL_ProcFormatString =
    {
        0,
        {

	/* Procedure OnUserAuthenticated */

			0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/*  2 */	NdrFcLong( 0x0 ),	/* 0 */
/*  6 */	NdrFcShort( 0x3 ),	/* 3 */
/*  8 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 10 */	NdrFcShort( 0x0 ),	/* 0 */
/* 12 */	NdrFcShort( 0x8 ),	/* 8 */
/* 14 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x5,		/* 5 */
/* 16 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 18 */	NdrFcShort( 0x0 ),	/* 0 */
/* 20 */	NdrFcShort( 0x1 ),	/* 1 */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter credential */

/* 24 */	NdrFcShort( 0xb ),	/* Flags:  must size, must free, in, */
/* 26 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 28 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Parameter username */

/* 30 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 32 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 34 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter password */

/* 36 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 38 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 40 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter sid */

/* 42 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 44 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 46 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 48 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 50 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 52 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Terminate */


	/* Procedure HasInternetConnection */

/* 54 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 56 */	NdrFcLong( 0x0 ),	/* 0 */
/* 60 */	NdrFcShort( 0x4 ),	/* 4 */
/* 62 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 64 */	NdrFcShort( 0x0 ),	/* 0 */
/* 66 */	NdrFcShort( 0x8 ),	/* 8 */
/* 68 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 70 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 72 */	NdrFcShort( 0x0 ),	/* 0 */
/* 74 */	NdrFcShort( 0x0 ),	/* 0 */
/* 76 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */


	/* Return value */

/* 78 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 80 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 82 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure SetReauthCheckDoneEvent */

/* 84 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 86 */	NdrFcLong( 0x0 ),	/* 0 */
/* 90 */	NdrFcShort( 0x3 ),	/* 3 */
/* 92 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 94 */	NdrFcShort( 0x8 ),	/* 8 */
/* 96 */	NdrFcShort( 0x8 ),	/* 8 */
/* 98 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 100 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 102 */	NdrFcShort( 0x0 ),	/* 0 */
/* 104 */	NdrFcShort( 0x0 ),	/* 0 */
/* 106 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter event */

/* 108 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 110 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 112 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 114 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 116 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 118 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure SetHasInternetConnection */

/* 120 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 122 */	NdrFcLong( 0x0 ),	/* 0 */
/* 126 */	NdrFcShort( 0x4 ),	/* 4 */
/* 128 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 130 */	NdrFcShort( 0x6 ),	/* 6 */
/* 132 */	NdrFcShort( 0x8 ),	/* 8 */
/* 134 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 136 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 138 */	NdrFcShort( 0x0 ),	/* 0 */
/* 140 */	NdrFcShort( 0x0 ),	/* 0 */
/* 142 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter hic */

/* 144 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 146 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 148 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Return value */

/* 150 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 152 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 154 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Initialize */

/* 156 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 158 */	NdrFcLong( 0x0 ),	/* 0 */
/* 162 */	NdrFcShort( 0x3 ),	/* 3 */
/* 164 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 166 */	NdrFcShort( 0x0 ),	/* 0 */
/* 168 */	NdrFcShort( 0x8 ),	/* 8 */
/* 170 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 172 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 174 */	NdrFcShort( 0x0 ),	/* 0 */
/* 176 */	NdrFcShort( 0x0 ),	/* 0 */
/* 178 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter provider */

/* 180 */	NdrFcShort( 0xb ),	/* Flags:  must size, must free, in, */
/* 182 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 184 */	NdrFcShort( 0x38 ),	/* Type Offset=56 */

	/* Return value */

/* 186 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 188 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 190 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure FinishAuthentication */

/* 192 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 194 */	NdrFcLong( 0x0 ),	/* 0 */
/* 198 */	NdrFcShort( 0x5 ),	/* 5 */
/* 200 */	NdrFcShort( 0x1c ),	/* x86 Stack size/offset = 28 */
/* 202 */	NdrFcShort( 0x0 ),	/* 0 */
/* 204 */	NdrFcShort( 0x8 ),	/* 8 */
/* 206 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x6,		/* 6 */
/* 208 */	0x8,		/* 8 */
			0x7,		/* Ext Flags:  new corr desc, clt corr check, srv corr check, */
/* 210 */	NdrFcShort( 0x1 ),	/* 1 */
/* 212 */	NdrFcShort( 0x1 ),	/* 1 */
/* 214 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter username */

/* 216 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 218 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 220 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter password */

/* 222 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 224 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 226 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter fullname */

/* 228 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 230 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 232 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter sid */

/* 234 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 236 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 238 */	NdrFcShort( 0x52 ),	/* Type Offset=82 */

	/* Parameter error_text */

/* 240 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 242 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 244 */	NdrFcShort( 0x52 ),	/* Type Offset=82 */

	/* Return value */

/* 246 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 248 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 250 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure OnUserAuthenticated */

/* 252 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 254 */	NdrFcLong( 0x0 ),	/* 0 */
/* 258 */	NdrFcShort( 0x6 ),	/* 6 */
/* 260 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 262 */	NdrFcShort( 0x0 ),	/* 0 */
/* 264 */	NdrFcShort( 0x8 ),	/* 8 */
/* 266 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 268 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 270 */	NdrFcShort( 0x0 ),	/* 0 */
/* 272 */	NdrFcShort( 0x1 ),	/* 1 */
/* 274 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter username */

/* 276 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 278 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 280 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter password */

/* 282 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 284 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 286 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter sid */

/* 288 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 290 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 292 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 294 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 296 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 298 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure ReportError */

/* 300 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 302 */	NdrFcLong( 0x0 ),	/* 0 */
/* 306 */	NdrFcShort( 0x7 ),	/* 7 */
/* 308 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 310 */	NdrFcShort( 0x10 ),	/* 16 */
/* 312 */	NdrFcShort( 0x8 ),	/* 8 */
/* 314 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 316 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 318 */	NdrFcShort( 0x0 ),	/* 0 */
/* 320 */	NdrFcShort( 0x1 ),	/* 1 */
/* 322 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter status */

/* 324 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 326 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 328 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter substatus */

/* 330 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 332 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 334 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter status_text */

/* 336 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 338 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 340 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 342 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 344 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 346 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure SetUserInfo */

/* 348 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 350 */	NdrFcLong( 0x0 ),	/* 0 */
/* 354 */	NdrFcShort( 0x3 ),	/* 3 */
/* 356 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 358 */	NdrFcShort( 0x0 ),	/* 0 */
/* 360 */	NdrFcShort( 0x8 ),	/* 8 */
/* 362 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 364 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 366 */	NdrFcShort( 0x0 ),	/* 0 */
/* 368 */	NdrFcShort( 0x1 ),	/* 1 */
/* 370 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter sid */

/* 372 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 374 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 376 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Parameter email */

/* 378 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 380 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 382 */	NdrFcShort( 0x2e ),	/* Type Offset=46 */

	/* Return value */

/* 384 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 386 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 388 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

			0x0
        }
    };

static const gaia_credential_provider_MIDL_TYPE_FORMAT_STRING gaia_credential_provider__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */
/*  2 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/*  4 */	NdrFcLong( 0x0 ),	/* 0 */
/*  8 */	NdrFcShort( 0x0 ),	/* 0 */
/* 10 */	NdrFcShort( 0x0 ),	/* 0 */
/* 12 */	0xc0,		/* 192 */
			0x0,		/* 0 */
/* 14 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 16 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 18 */	0x0,		/* 0 */
			0x46,		/* 70 */
/* 20 */	
			0x12, 0x0,	/* FC_UP */
/* 22 */	NdrFcShort( 0xe ),	/* Offset= 14 (36) */
/* 24 */	
			0x1b,		/* FC_CARRAY */
			0x1,		/* 1 */
/* 26 */	NdrFcShort( 0x2 ),	/* 2 */
/* 28 */	0x9,		/* Corr desc: FC_ULONG */
			0x0,		/*  */
/* 30 */	NdrFcShort( 0xfffc ),	/* -4 */
/* 32 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 34 */	0x6,		/* FC_SHORT */
			0x5b,		/* FC_END */
/* 36 */	
			0x17,		/* FC_CSTRUCT */
			0x3,		/* 3 */
/* 38 */	NdrFcShort( 0x8 ),	/* 8 */
/* 40 */	NdrFcShort( 0xfff0 ),	/* Offset= -16 (24) */
/* 42 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 44 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 46 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 48 */	NdrFcShort( 0x0 ),	/* 0 */
/* 50 */	NdrFcShort( 0x4 ),	/* 4 */
/* 52 */	NdrFcShort( 0x0 ),	/* 0 */
/* 54 */	NdrFcShort( 0xffde ),	/* Offset= -34 (20) */
/* 56 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 58 */	NdrFcLong( 0xcec9ef6c ),	/* -825626772 */
/* 62 */	NdrFcShort( 0xb2e6 ),	/* -19738 */
/* 64 */	NdrFcShort( 0x4bb6 ),	/* 19382 */
/* 66 */	0x8f,		/* 143 */
			0x1e,		/* 30 */
/* 68 */	0x17,		/* 23 */
			0x47,		/* 71 */
/* 70 */	0xba,		/* 186 */
			0x4f,		/* 79 */
/* 72 */	0x71,		/* 113 */
			0x38,		/* 56 */
/* 74 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 76 */	NdrFcShort( 0x6 ),	/* Offset= 6 (82) */
/* 78 */	
			0x13, 0x0,	/* FC_OP */
/* 80 */	NdrFcShort( 0xffd4 ),	/* Offset= -44 (36) */
/* 82 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 84 */	NdrFcShort( 0x0 ),	/* 0 */
/* 86 */	NdrFcShort( 0x4 ),	/* 4 */
/* 88 */	NdrFcShort( 0x0 ),	/* 0 */
/* 90 */	NdrFcShort( 0xfff4 ),	/* Offset= -12 (78) */

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
            }

        };



/* Object interface: IUnknown, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: IGaiaCredentialProvider, ver. 0.0,
   GUID={0xCEC9EF6C,0xB2E6,0x4BB6,{0x8F,0x1E,0x17,0x47,0xBA,0x4F,0x71,0x38}} */

#pragma code_seg(".orpc")
static const unsigned short IGaiaCredentialProvider_FormatStringOffsetTable[] =
    {
    0,
    54
    };

static const MIDL_STUBLESS_PROXY_INFO IGaiaCredentialProvider_ProxyInfo =
    {
    &Object_StubDesc,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IGaiaCredentialProvider_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IGaiaCredentialProvider_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IGaiaCredentialProvider_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(5) _IGaiaCredentialProviderProxyVtbl = 
{
    &IGaiaCredentialProvider_ProxyInfo,
    &IID_IGaiaCredentialProvider,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IGaiaCredentialProvider::OnUserAuthenticated */ ,
    (void *) (INT_PTR) -1 /* IGaiaCredentialProvider::HasInternetConnection */
};

const CInterfaceStubVtbl _IGaiaCredentialProviderStubVtbl =
{
    &IID_IGaiaCredentialProvider,
    &IGaiaCredentialProvider_ServerInfo,
    5,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Standard interface: __MIDL_itf_gaia_credential_provider_0000_0001, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IGaiaCredentialProviderForTesting, ver. 0.0,
   GUID={0x224CE2FB,0x2977,0x4585,{0xBD,0x46,0x1B,0xAE,0x8D,0x79,0x64,0xDE}} */

#pragma code_seg(".orpc")
static const unsigned short IGaiaCredentialProviderForTesting_FormatStringOffsetTable[] =
    {
    84,
    120
    };

static const MIDL_STUBLESS_PROXY_INFO IGaiaCredentialProviderForTesting_ProxyInfo =
    {
    &Object_StubDesc,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IGaiaCredentialProviderForTesting_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IGaiaCredentialProviderForTesting_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IGaiaCredentialProviderForTesting_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(5) _IGaiaCredentialProviderForTestingProxyVtbl = 
{
    &IGaiaCredentialProviderForTesting_ProxyInfo,
    &IID_IGaiaCredentialProviderForTesting,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IGaiaCredentialProviderForTesting::SetReauthCheckDoneEvent */ ,
    (void *) (INT_PTR) -1 /* IGaiaCredentialProviderForTesting::SetHasInternetConnection */
};

const CInterfaceStubVtbl _IGaiaCredentialProviderForTestingStubVtbl =
{
    &IID_IGaiaCredentialProviderForTesting,
    &IGaiaCredentialProviderForTesting_ServerInfo,
    5,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IGaiaCredential, ver. 0.0,
   GUID={0xE5BF88DF,0x9966,0x465B,{0xB2,0x33,0xC1,0xCA,0xC7,0x51,0x0A,0x59}} */

#pragma code_seg(".orpc")
static const unsigned short IGaiaCredential_FormatStringOffsetTable[] =
    {
    156,
    54,
    192,
    252,
    300
    };

static const MIDL_STUBLESS_PROXY_INFO IGaiaCredential_ProxyInfo =
    {
    &Object_StubDesc,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IGaiaCredential_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IGaiaCredential_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IGaiaCredential_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(8) _IGaiaCredentialProxyVtbl = 
{
    &IGaiaCredential_ProxyInfo,
    &IID_IGaiaCredential,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IGaiaCredential::Initialize */ ,
    (void *) (INT_PTR) -1 /* IGaiaCredential::Terminate */ ,
    (void *) (INT_PTR) -1 /* IGaiaCredential::FinishAuthentication */ ,
    (void *) (INT_PTR) -1 /* IGaiaCredential::OnUserAuthenticated */ ,
    (void *) (INT_PTR) -1 /* IGaiaCredential::ReportError */
};

const CInterfaceStubVtbl _IGaiaCredentialStubVtbl =
{
    &IID_IGaiaCredential,
    &IGaiaCredential_ServerInfo,
    8,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IReauthCredential, ver. 0.0,
   GUID={0xCC75BCEA,0xA636,0x4798,{0xBF,0x8E,0x0F,0xF6,0x4D,0x74,0x34,0x51}} */

#pragma code_seg(".orpc")
static const unsigned short IReauthCredential_FormatStringOffsetTable[] =
    {
    348
    };

static const MIDL_STUBLESS_PROXY_INFO IReauthCredential_ProxyInfo =
    {
    &Object_StubDesc,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IReauthCredential_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO IReauthCredential_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    gaia_credential_provider__MIDL_ProcFormatString.Format,
    &IReauthCredential_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(4) _IReauthCredentialProxyVtbl = 
{
    &IReauthCredential_ProxyInfo,
    &IID_IReauthCredential,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* IReauthCredential::SetUserInfo */
};

const CInterfaceStubVtbl _IReauthCredentialStubVtbl =
{
    &IID_IReauthCredential,
    &IReauthCredential_ServerInfo,
    4,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};

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
    gaia_credential_provider__MIDL_TypeFormatString.Format,
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

const CInterfaceProxyVtbl * const _gaia_credential_provider_ProxyVtblList[] = 
{
    ( CInterfaceProxyVtbl *) &_IGaiaCredentialProviderProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IGaiaCredentialProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IReauthCredentialProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IGaiaCredentialProviderForTestingProxyVtbl,
    0
};

const CInterfaceStubVtbl * const _gaia_credential_provider_StubVtblList[] = 
{
    ( CInterfaceStubVtbl *) &_IGaiaCredentialProviderStubVtbl,
    ( CInterfaceStubVtbl *) &_IGaiaCredentialStubVtbl,
    ( CInterfaceStubVtbl *) &_IReauthCredentialStubVtbl,
    ( CInterfaceStubVtbl *) &_IGaiaCredentialProviderForTestingStubVtbl,
    0
};

PCInterfaceName const _gaia_credential_provider_InterfaceNamesList[] = 
{
    "IGaiaCredentialProvider",
    "IGaiaCredential",
    "IReauthCredential",
    "IGaiaCredentialProviderForTesting",
    0
};


#define _gaia_credential_provider_CHECK_IID(n)	IID_GENERIC_CHECK_IID( _gaia_credential_provider, pIID, n)

int __stdcall _gaia_credential_provider_IID_Lookup( const IID * pIID, int * pIndex )
{
    IID_BS_LOOKUP_SETUP

    IID_BS_LOOKUP_INITIAL_TEST( _gaia_credential_provider, 4, 2 )
    IID_BS_LOOKUP_NEXT_TEST( _gaia_credential_provider, 1 )
    IID_BS_LOOKUP_RETURN_RESULT( _gaia_credential_provider, 4, *pIndex )
    
}

const ExtendedProxyFileInfo gaia_credential_provider_ProxyFileInfo = 
{
    (PCInterfaceProxyVtblList *) & _gaia_credential_provider_ProxyVtblList,
    (PCInterfaceStubVtblList *) & _gaia_credential_provider_StubVtblList,
    (const PCInterfaceName * ) & _gaia_credential_provider_InterfaceNamesList,
    0, /* no delegation */
    & _gaia_credential_provider_IID_Lookup, 
    4,
    2,
    0, /* table of [async_uuid] interfaces */
    0, /* Filler1 */
    0, /* Filler2 */
    0  /* Filler3 */
};
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* !defined(_M_IA64) && !defined(_M_AMD64) && !defined(_ARM_) */

