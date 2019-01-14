

/* this ALWAYS GENERATED file contains the IIDs and CLSIDs */

/* link this file in with the server and any clients */


 /* File created by MIDL compiler version 8.xx.xxxx */
/* at a redacted point in time
 */
/* Compiler settings for ../../chrome/credential_provider/gaiacp/gaia_credential_provider.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.xx.xxxx 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


#ifdef __cplusplus
extern "C"{
#endif 


#include <rpc.h>
#include <rpcndr.h>

#ifdef _MIDL_USE_GUIDDEF_

#ifndef INITGUID
#define INITGUID
#include <guiddef.h>
#undef INITGUID
#else
#include <guiddef.h>
#endif

#define MIDL_DEFINE_GUID(type,name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
        DEFINE_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8)

#else // !_MIDL_USE_GUIDDEF_

#ifndef __IID_DEFINED__
#define __IID_DEFINED__

typedef struct _IID
{
    unsigned long x;
    unsigned short s1;
    unsigned short s2;
    unsigned char  c[8];
} IID;

#endif // __IID_DEFINED__

#ifndef CLSID_DEFINED
#define CLSID_DEFINED
typedef IID CLSID;
#endif // CLSID_DEFINED

#define MIDL_DEFINE_GUID(type,name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
        EXTERN_C __declspec(selectany) const type name = {l,w1,w2,{b1,b2,b3,b4,b5,b6,b7,b8}}

#endif // !_MIDL_USE_GUIDDEF_

MIDL_DEFINE_GUID(IID, IID_IGaiaCredentialProvider,0xCEC9EF6C,0xB2E6,0x4BB6,0x8F,0x1E,0x17,0x47,0xBA,0x4F,0x71,0x38);


MIDL_DEFINE_GUID(IID, IID_IGaiaCredentialProviderForTesting,0x224CE2FB,0x2977,0x4585,0xBD,0x46,0x1B,0xAE,0x8D,0x79,0x64,0xDE);


MIDL_DEFINE_GUID(IID, IID_IGaiaCredential,0xE5BF88DF,0x9966,0x465B,0xB2,0x33,0xC1,0xCA,0xC7,0x51,0x0A,0x59);


MIDL_DEFINE_GUID(IID, IID_IReauthCredential,0xCC75BCEA,0xA636,0x4798,0xBF,0x8E,0x0F,0xF6,0x4D,0x74,0x34,0x51);


MIDL_DEFINE_GUID(IID, LIBID_GaiaCredentialProviderLib,0x4ADC3A52,0x8673,0x4CE3,0x81,0xF6,0x83,0x3D,0x18,0xBE,0xEB,0xA2);


MIDL_DEFINE_GUID(CLSID, CLSID_GaiaCredentialProvider,0x0B5BFDF0,0x4594,0x47AC,0x94,0x0A,0xCF,0xC6,0x9A,0xBC,0x56,0x1C);


MIDL_DEFINE_GUID(CLSID, CLSID_GaiaCredential,0x44AF95AC,0x6B23,0x4C54,0x94,0xBE,0xED,0xB1,0xCB,0x52,0xDA,0xFD);


MIDL_DEFINE_GUID(CLSID, CLSID_ReauthCredential,0xE6CC5D8B,0x54C2,0x4586,0xAD,0xC3,0x74,0x8E,0xD1,0x62,0x84,0xB7);

#undef MIDL_DEFINE_GUID

#ifdef __cplusplus
}
#endif



