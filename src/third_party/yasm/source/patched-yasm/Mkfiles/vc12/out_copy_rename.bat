@echo off
if not exist %1 goto nofile
if exist %2 goto copy

echo creating directory %2
md %2 > nul

:copy
set str=%2
for /f "useback tokens=*" %%a in ('%str%') do set str=%%~a
set str=%str:~-1%
if %str% == "\" goto hasbackslash

if not exist %2\%3 goto cpy
fc %1 %2\%3 > nul && if not %errorlevel 1 goto exit
echo overwriting %2\%3 with %1
copy %1 %2\%3 > nul
goto exit

:cpy
echo copying %1 to %2\%3
copy %1 %2\%3 > nul
goto exit

:hasbackslash
if not exist %2%3 goto cpy2 
fc %1 %2%3 > nul && if not %errorlevel 1 goto exit
echo overwriting %2%3 with %1
copy %1 %2%3 > nul
goto exit

:cpy2
echo copying %1 to %2%3
copy %1 %2%3 > nul
goto exit

:nofile
echo %1 not found

:exit


