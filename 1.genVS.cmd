@echo off
if not exist "build" (
	mkdir build
)
cmake . -B build -DCMAKE_TOOLCHAIN_FILE=D:/Codes/vcpkg/scripts/buildsystems/vcpkg.cmake -DPBRT_OPTIX_PATH="C:\ProgramData\NVIDIA Corporation\OptiX SDK 8.1.0"