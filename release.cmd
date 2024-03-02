@echo off

if {%1}=={} (
	echo Usage: release ^<version-id^>
	exit /b 0
)

@where /q devenv.exe >nul
if errorlevel 1 (
	echo Unable to find Visual C++ IDE ^(devenv.exe^) in current path.
	exit /b 0
)

@where /q zip.exe >nul
if errorlevel 1 (
	echo Unable to find Info-Zip ^(zip.exe^) in current path.
	exit /b 0
)

set _verid=%1

setlocal enabledelayedexpansion

if not exist out md out
if not exist out\debug md out\debug
if not exist out\release md out\release
if not exist publish md publish
if exist publish\build.log del publish\build.log
if exist publish\Altirra-!_verid!-src.zip del publish\Altirra-!_verid!-src.zip
if exist publish\Altirra-!_verid!.zip del publish\Altirra-!_verid!.zip

set _abverfile=src\Altirra\autobuild\version.h
set _abverfile2=src\Kernel\autobuild\version.inc

if not exist src\Altirra\autobuild md src\Altirra\autobuild
if not exist src\Kernel\autobuild md src\Kernel\autobuild

devenv src\Altirra.sln /Clean Release^|Win32
devenv src\Altirra.sln /Clean Release^|x64

echo #ifndef AT_VERSION_H >%_abverfile%
echo #define AT_VERSION_H >>%_abverfile%
echo #define AT_VERSION "%_verid%" >>%_abverfile%

echo "%_verid%" | find "-" >nul
if errorlevel 1 (
	echo #define AT_VERSION_PRERELEASE 0 >>%_abverfile%
) else (
	echo #define AT_VERSION_PRERELEASE 1 >>%_abverfile%
)

echo #endif >>%_abverfile%

echo .macro _VERSIONSTR_INTERNAL >%_abverfile2%
echo 	dta d"%_verid%" >>%_abverfile2%
echo .endm >>%_abverfile2%

devenv src\Altirra.sln /Build Debug /Project Kernel /Out publish\build-debug.log
if errorlevel 1 (
	call :reportBuildFailure publish\build-debug.log
	goto :cleanup
)

devenv src\Altirra.sln /Rebuild Release^|Win32 /Out publish\build.log
if errorlevel 1 (
	call :reportBuildFailure publish\build.log
	goto :cleanup
)

devenv src\Altirra.sln /Rebuild Release^|x64 /Out publish\build-x64.log
if errorlevel 1 (
	call :reportBuildFailure publish\build-x64.log
	goto :cleanup
)

devenv src\ATHelpFile.sln /Rebuild Release /Out publish\build-x64-debug.log
if errorlevel 1 (
	call :reportBuildFailure publish\build-x64-debug.log
	goto :cleanup
)

zip -9 -X -r publish\Altirra-!_verid!-src.zip ^
	src ^
	src\Kasumi\data\Tuffy.* ^
	Copying ^
	-i ^
	*.vcproj ^
	*.sln ^
	*.cpp ^
	*.h ^
	*.fx ^
	*.rules ^
	*.asm ^
	*.xasm ^
	*.vsprops ^
	*.rc ^
	*.asm64 ^
	*.inl ^
	*.fxh ^
	*.vdfx ^
	*.inc ^
	*.k ^
	*.txt ^
	*.bmp ^
	*.ico ^
	*.cur ^
	*.manifest ^
	*.s ^
	*.pcm

zip -9 -X publish\Altirra-!_verid!-src.zip ^
	Copying ^
	release.cmd ^
	src\Kasumi\data\Tuffy.* ^
	src\Kernel\source\shared\atarifont.bin ^
	src\Kernel\source\shared\atariifont.bin ^
	src\Kernel\Makefile ^
	src\HLEKernel\Makefile ^
	src\ATBasic\makefile ^
	src\ATHelpFile\source\*.xml ^
	src\ATHelpFile\source\*.xsl ^
	src\ATHelpFile\source\*.css ^
	src\ATHelpFile\source\*.hhp ^
	src\ATHelpFile\source\*.hhw ^
	src\ATHelpFile\source\*.hhc ^
	src\Altirra\res\altirraexticons.res ^
	out\debug\kernel.rom ^
	out\release\kernel.rom

zip -9 -X -j publish\Altirra-!_verid!.zip ^
	out\release\Altirra.exe ^
	out\releaseamd64\Altirra64.exe ^
	Copying ^
	out\Helpfile\Altirra.chm
copy out\release\Altirra.pdb publish\Altirra-!_verid!.pdb
copy out\releaseamd64\Altirra64.pdb publish\Altirra64-!_verid!.pdb

dir publish
if exist src\Altirra\autobuild\version.h del src\Altirra\autobuild\version.h
if exist src\Kernel\autobuild\version.h del src\Kernel\autobuild\version.h
exit /b 0

:reportBuildFailure
echo.
echo ============ BUILD FAILED ============

findstr /r "^[0-9]*>*[a-zA-Z0-9:\/]*[ ]*\([0-9][0-9]*\).*error.*" "%1"
echo ============ BUILD FAILED ============
goto :cleanup

:cleanup
if exist src\Altirra\autobuild\version.h del src\Altirra\autobuild\version.h
exit /b 5
