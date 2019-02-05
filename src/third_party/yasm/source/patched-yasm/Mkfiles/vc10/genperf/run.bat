cd ..\..\..
@echo off

for /f "usebackq tokens=1*" %%f in (`reg query HKCR\Applications\python.exe\shell\open\command  2^>NUL`) do (set _my_=%%f %%g)
goto try1%errorlevel%

:try10
goto ok

:try11
for /f "usebackq tokens=1*" %%f in (`reg query HKCR\Python.File\shell\open\command 2^>NUL`) do (set _my_=%%f %%g)
goto try2%errorlevel%

:try20:
goto ok

:try21:
echo Building without Python ...
goto therest

:ok
echo Building with Python ...
set _res_=%_my_:*REG_SZ=%
set _end_=%_res_:*exe"=%
call set _python_=%%_res_:%_end_%=%%
call %_python_% modules\arch\x86\gen_x86_insn.py

:therest
@echo on
call :update %1 x86insn_nasm.gperf x86insn_nasm.c
call :update %1 x86insn_gas.gperf x86insn_gas.c
call :update %1 modules\arch\x86\x86cpu.gperf x86cpu.c
call :update %1 modules\arch\x86\x86regtmod.gperf x86regtmod.c
goto :eof

:update
%1 %2 tf
call mkfiles\vc10\out_copy_rename tf .\ %3
del tf
