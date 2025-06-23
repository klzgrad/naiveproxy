@echo off
setlocal

rem Create required directories.
mkdir bin\dll bin\static bin\example bin\include

rem Copy files using a subroutine. Exits immediately on failure.
call :copyFile "tests\fullbench.c" "bin\example\"
call :copyFile "programs\datagen.c" "bin\example\"
call :copyFile "programs\datagen.h" "bin\example\"
call :copyFile "programs\util.h" "bin\example\"
call :copyFile "programs\platform.h" "bin\example\"
call :copyFile "lib\common\mem.h" "bin\example\"
call :copyFile "lib\common\zstd_internal.h" "bin\example\"
call :copyFile "lib\common\error_private.h" "bin\example\"
call :copyFile "lib\common\xxhash.h" "bin\example\"
call :copyFile "lib\libzstd.a" "bin\static\libzstd_static.lib"
call :copyFile "lib\dll\libzstd.*" "bin\dll\"
call :copyFile "lib\dll\example\Makefile" "bin\example\"
call :copyFile "lib\dll\example\fullbench-dll.*" "bin\example\"
call :copyFile "lib\dll\example\README.md" "bin\"
call :copyFile "lib\zstd.h" "bin\include\"
call :copyFile "lib\zstd_errors.h" "bin\include\"
call :copyFile "lib\zdict.h" "bin\include\"
call :copyFile "programs\zstd.exe" "bin\zstd.exe"

endlocal
exit /b 0

:copyFile
copy "%~1" "%~2"
if errorlevel 1 (
    echo Failed to copy "%~1"
    exit 1
)
exit /b
