cmake_minimum_required (VERSION 2.6)
project (Riza)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "-msse3 -D_WIN32_WINNT=0x0500 -DWINVER=0x0500 -D_WIN32_IE=0x0500")

include_directories(
	.
	h
	../h
	c:/dx9sdk_feb2010/include
)

add_library(Riza
#	source/audioout.cpp
	source/audiocodec.cpp
	source/audioformat.cpp
	source/bitmap.cpp
	source/direct3d.cpp
	source/display.cpp
#	source/displayd3d101.cpp
	source/displayd3dfx.cpp
	source/displayddraw.cpp
	source/displaydrv.cpp
	source/displaydx9.cpp
	source/displaygdi.cpp
	source/displaygl.cpp
	source/displaymgr.cpp
	source/opengl.cpp
#	source/w32audiocodec.cpp
	source/w32videocodec.cpp
	source/w32videocodecpack.cpp
)
