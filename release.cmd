@echo off

if {%1}=={} (
	echo Usage: release ^<version-id^>
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

if not exist src\Altirra\autobuild md src\Altirra\autobuild

devenv src\Altirra.sln /Clean Release

echo #ifndef AT_VERSION_H >%_abverfile%
echo #define AT_VERSION_H >>%_abverfile%
echo #define AT_VERSION "%_verid%" >>%_abverfile%
echo #endif >>%_abverfile%

devenv src\Altirra.sln /Build Debug /Project Kernel /Out publish\build-debug.log
if errorlevel 1 (
	echo Build failed!
	goto :cleanup
)

devenv src\Altirra.sln /Rebuild Release /Out publish\build.log
if errorlevel 1 (
	echo Build failed!
	goto :cleanup
)

zip -9 -X -r publish\Altirra-!_verid!-src.zip ^
	src ^
	src\Kasumi\data\Tuffy.* ^
	src\Kernel\Makefile ^
	-i *.vcproj *.sln *.cpp *.h *.fx *.rules *.asm *.xasm *.vsprops *.rc *.asm64 *.inl *.fxh *.vdfx *.inc *.k *.txt

zip -9 -X publish\Altirra-!_verid!-src.zip ^
	Copying ^
	README.txt ^
	release.cmd ^
	src\Kasumi\data\Tuffy.* ^
	src\Kernel\source\atarifont.bin ^
	src\Kernel\Makefile ^
	src\HLEKernel\Makefile ^
	out\debug\kernel.rom ^
	out\release\kernel.rom


zip -9 -X -j publish\Altirra-!_verid!.zip out\release\Altirra.exe copying README.txt
copy out\release\Altirra.pdb publish\Altirra-!_verid!.pdb

dir publish
if exist src\Altirra\autobuild\version.h del src\Altirra\autobuild\version.h
exit /b 0

:cleanup
if exist src\Altirra\autobuild\version.h del src\Altirra\autobuild\version.h
exit /b 5
