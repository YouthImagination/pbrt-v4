@echo off
python "%~dp0patch_vcxproj.py"
cmake --build build --config Release -j 16