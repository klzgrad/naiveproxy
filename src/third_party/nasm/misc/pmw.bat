@echo off
rem some batch file to bind nasm and ndisasm with pmode/w
rem a mega cool dos extender for watcom done by tran
rem 
rem max 8 megs, dpmi stack 256*16=4096, no banner
pmwlite.exe nasm.exe
pmwsetup.exe /X8388608 /P256 /B0 nasm.exe
pmwlite.exe ndisasm.exe
pmwsetup.exe /X8388608 /P256 /B0 ndisasm.exe
