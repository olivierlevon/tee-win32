@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: build_pcre2.bat - Build PCRE2 static libraries for tee-win32
::
:: Builds PCRE2 (8-bit only) as a static library for all platforms and configs:
::   Win32/x86, x64, ARM64  x  Debug, Release
::
:: Prerequisites:
::   - CMake (in PATH)
::   - Visual Studio 2022 (v143 toolset)
::   - PCRE2 source in deps\pcre2\ (git clone https://github.com/PCRE2Project/pcre2.git)
::
:: Output:
::   lib\Win32\Debug\pcre2-8-static.lib
::   lib\Win32\Release\pcre2-8-static.lib
::   lib\x64\Debug\pcre2-8-static.lib
::   lib\x64\Release\pcre2-8-static.lib
::   lib\ARM64\Debug\pcre2-8-static.lib
::   lib\ARM64\Release\pcre2-8-static.lib
::   deps\pcre2\include\pcre2.h  (generated public header)
:: ============================================================================

set PCRE2_SRC=deps\pcre2
set BUILD_BASE=deps\pcre2\build
set LIB_OUT=lib
set INCLUDE_OUT=deps\pcre2\include
set HEADER_COPIED=0
set ERRORS=0

:: ---------------------------------------------------------------------------
:: Check prerequisites
:: ---------------------------------------------------------------------------
if not exist "%PCRE2_SRC%\CMakeLists.txt" (
    echo [ERROR] PCRE2 source not found in %PCRE2_SRC%\
    echo.
    echo   git clone https://github.com/PCRE2Project/pcre2.git %PCRE2_SRC%
    echo.
    exit /b 1
)

where cmake >nul 2>&1 || (
    echo [ERROR] CMake not found in PATH
    exit /b 1
)

:: ---------------------------------------------------------------------------
:: Common CMake options (shared by all builds)
::
::   BUILD_SHARED_LIBS=OFF        Static library, no DLL
::   PCRE2_BUILD_PCRE2_8=ON       8-bit code units (UTF-8 / byte stream)
::   PCRE2_BUILD_PCRE2_16=OFF     No UTF-16 variant needed
::   PCRE2_BUILD_PCRE2_32=OFF     No UTF-32 variant needed
::   PCRE2_BUILD_TESTS=OFF        Skip test suite
::   PCRE2_BUILD_PCRE2GREP=OFF    Skip pcre2grep tool
::   PCRE2_SUPPORT_JIT=OFF        No JIT (avoids VirtualAlloc/CRT deps)
::   PCRE2_SUPPORT_UNICODE=OFF    No Unicode tables (patterns are ASCII,
::                                 data is raw byte stream; saves ~120KB)
::   PCRE2_NEWLINE=ANYCRLF        Recognize \r, \n, \r\n as newlines
::   PCRE2_SUPPORT_BSR_ANYCRLF=ON \R matches CR/LF/CRLF only
:: ---------------------------------------------------------------------------
set COMMON_OPTS=^
 -DBUILD_SHARED_LIBS=OFF^
 -DPCRE2_BUILD_PCRE2_8=ON^
 -DPCRE2_BUILD_PCRE2_16=OFF^
 -DPCRE2_BUILD_PCRE2_32=OFF^
 -DPCRE2_BUILD_TESTS=OFF^
 -DPCRE2_BUILD_PCRE2GREP=OFF^
 -DPCRE2_SUPPORT_JIT=OFF^
 -DPCRE2_SUPPORT_UNICODE=OFF^
 -DPCRE2_NEWLINE=ANYCRLF^
 -DPCRE2_SUPPORT_BSR_ANYCRLF=ON

:: ---------------------------------------------------------------------------
:: Build loop: 3 platforms x 2 configs = 6 builds
::
:: Debug   -> PCRE2_STATIC_RUNTIME=OFF  -> /MDd  (matches tee Debug config)
:: Release -> PCRE2_STATIC_RUNTIME=ON   -> /MT   (matches tee Release config)
:: ---------------------------------------------------------------------------
for %%P in (Win32 x64 ARM64) do (
    for %%C in (Debug Release) do (
        echo.
        echo ====================================================================
        echo  Building PCRE2 -- Platform: %%P, Configuration: %%C
        echo ====================================================================

        set STATIC_RT=OFF
        if "%%C"=="Release" set STATIC_RT=ON

        set BDIR=%BUILD_BASE%\%%P-%%C

        set EXTRA_FLAGS=
        if "%%C"=="Release" set EXTRA_FLAGS=-DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG /GS- /Gs9999999"

        cmake -G "Visual Studio 17 2022" -A %%P ^
            -S %PCRE2_SRC% -B !BDIR! ^
            %COMMON_OPTS% ^
            -DPCRE2_STATIC_RUNTIME=!STATIC_RT! !EXTRA_FLAGS!
        if errorlevel 1 (
            echo [ERROR] CMake configure failed for %%P/%%C
            set /a ERRORS+=1
            goto :next
        )

        cmake --build !BDIR! --config %%C
        if errorlevel 1 (
            echo [ERROR] CMake build failed for %%P/%%C
            set /a ERRORS+=1
            goto :next
        )

        :: Copy library to output directory
        if not exist "%LIB_OUT%\%%P\%%C" mkdir "%LIB_OUT%\%%P\%%C"

        if "%%C"=="Debug" (
            :: Debug lib has 'd' suffix; normalize to pcre2-8-static.lib
            copy /Y "!BDIR!\%%C\pcre2-8-staticd.lib" "%LIB_OUT%\%%P\%%C\pcre2-8-static.lib" >nul
        ) else (
            copy /Y "!BDIR!\%%C\pcre2-8-static.lib" "%LIB_OUT%\%%P\%%C\pcre2-8-static.lib" >nul
        )

        if errorlevel 1 (
            echo [ERROR] Failed to copy library for %%P/%%C
            set /a ERRORS+=1
            goto :next
        )

        echo [OK] %LIB_OUT%\%%P\%%C\pcre2-8-static.lib

        :: Copy generated pcre2.h once (identical across platforms)
        if !HEADER_COPIED!==0 (
            if not exist "%INCLUDE_OUT%" mkdir "%INCLUDE_OUT%"
            copy /Y "!BDIR!\interface\pcre2.h" "%INCLUDE_OUT%\pcre2.h" >nul
            if not errorlevel 1 (
                echo [OK] %INCLUDE_OUT%\pcre2.h
                set HEADER_COPIED=1
            )
        )

        :next
    )
)

:: ---------------------------------------------------------------------------
:: Summary
:: ---------------------------------------------------------------------------
echo.
echo ====================================================================
if !ERRORS! gtr 0 (
    echo  DONE with !ERRORS! error^(s^). Check output above.
) else (
    echo  SUCCESS - All libraries built.
)
echo.
echo  Libraries:
for %%P in (Win32 x64 ARM64) do (
    for %%C in (Debug Release) do (
        if exist "%LIB_OUT%\%%P\%%C\pcre2-8-static.lib" (
            echo    %LIB_OUT%\%%P\%%C\pcre2-8-static.lib
        )
    )
)
echo.
echo  Header:
echo    %INCLUDE_OUT%\pcre2.h
echo ====================================================================

exit /b !ERRORS!
