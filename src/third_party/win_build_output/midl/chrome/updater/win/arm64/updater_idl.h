

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.xx.xxxx */
/* at a redacted point in time
 */
/* Compiler settings for ../../chrome/updater/win/com/updater_idl.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=ARM64 8.01.0622 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __updater_idl_h__
#define __updater_idl_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IUpdater_FWD_DEFINED__
#define __IUpdater_FWD_DEFINED__
typedef interface IUpdater IUpdater;

#endif 	/* __IUpdater_FWD_DEFINED__ */


#ifndef __IUpdater_FWD_DEFINED__
#define __IUpdater_FWD_DEFINED__
typedef interface IUpdater IUpdater;

#endif 	/* __IUpdater_FWD_DEFINED__ */


#ifndef __UpdaterClass_FWD_DEFINED__
#define __UpdaterClass_FWD_DEFINED__

#ifdef __cplusplus
typedef class UpdaterClass UpdaterClass;
#else
typedef struct UpdaterClass UpdaterClass;
#endif /* __cplusplus */

#endif 	/* __UpdaterClass_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __IUpdater_INTERFACE_DEFINED__
#define __IUpdater_INTERFACE_DEFINED__

/* interface IUpdater */
/* [unique][helpstring][uuid][dual][object] */ 


EXTERN_C const IID IID_IUpdater;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("63B8FFB1-5314-48C9-9C57-93EC8BC6184B")
    IUpdater : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE CheckForUpdate( 
            /* [string][in] */ const WCHAR *guid) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Update( 
            /* [string][in] */ const WCHAR *guid) = 0;
        
    };
    
    
#else 	/* C style interface */

    typedef struct IUpdaterVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IUpdater * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IUpdater * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IUpdater * This);
        
        HRESULT ( STDMETHODCALLTYPE *CheckForUpdate )( 
            IUpdater * This,
            /* [string][in] */ const WCHAR *guid);
        
        HRESULT ( STDMETHODCALLTYPE *Update )( 
            IUpdater * This,
            /* [string][in] */ const WCHAR *guid);
        
        END_INTERFACE
    } IUpdaterVtbl;

    interface IUpdater
    {
        CONST_VTBL struct IUpdaterVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IUpdater_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IUpdater_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IUpdater_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IUpdater_CheckForUpdate(This,guid)	\
    ( (This)->lpVtbl -> CheckForUpdate(This,guid) ) 

#define IUpdater_Update(This,guid)	\
    ( (This)->lpVtbl -> Update(This,guid) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IUpdater_INTERFACE_DEFINED__ */



#ifndef __UpdaterLib_LIBRARY_DEFINED__
#define __UpdaterLib_LIBRARY_DEFINED__

/* library UpdaterLib */
/* [helpstring][version][uuid] */ 



EXTERN_C const IID LIBID_UpdaterLib;

EXTERN_C const CLSID CLSID_UpdaterClass;

#ifdef __cplusplus

class DECLSPEC_UUID("158428A4-6014-4978-83BA-9FAD0DABE791")
UpdaterClass;
#endif
#endif /* __UpdaterLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


