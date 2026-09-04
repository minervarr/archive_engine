@echo off
REM archive_engine standalone build (Windows) — smoke-builds core/ with its
REM default module set (util + archive). See scripts/linux/build.sh for the
REM full explanation; this is the same build via MSVC + Ninja.
setlocal
cd /d "%~dp0..\.."

cmake -S core -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo.
echo Done: build\ (arc_util, arc_fs, arc_archive)
endlocal
