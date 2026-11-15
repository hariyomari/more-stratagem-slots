@echo off
rem Packages the mod into dist\more-stratagem-slots-<version>.zip with HellForge's CLI, which builds it first
rem (a mod that does not compile is not packed).
rem
rem   package.bat [HellForge folder]
rem
rem The HellForge folder is the installation (the one with app\ and mods\ in it). Without the argument it is taken
rem from the HELLFORGE environment variable.
setlocal

set "MOD=more-stratagem-slots"
set "ROOT=%~dp0"
set "HF=%~1"
if "%HF%"=="" set "HF=%HELLFORGE%"
if "%HF%"=="" (
    echo Give your HellForge folder: package.bat C:\HellForge  ^(or set the HELLFORGE environment variable^)
    exit /b 1
)
set "CLI=%HF%\app\cli\HellForge.Cli.exe"
if not exist "%CLI%" (
    echo No HellForge CLI at %CLI%
    exit /b 1
)

rem The CLI builds a mod only under an installation, so the mod is copied into its temp folder for the build.
set "WORK=%HF%\temp\package-%MOD%"
if exist "%WORK%" rmdir /s /q "%WORK%"
xcopy "%ROOT%%MOD%" "%WORK%\%MOD%\" /e /i /q >nul
if errorlevel 1 (
    echo Could not copy the mod to %WORK%
    exit /b 1
)
if not exist "%ROOT%dist" mkdir "%ROOT%dist"

"%CLI%" package-mod --mod "%WORK%\%MOD%" --out "%ROOT%dist"
set "RESULT=%ERRORLEVEL%"

rmdir /s /q "%WORK%"
exit /b %RESULT%
