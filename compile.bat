@echo off
REM Check if a command-line argument is provided
if "%~1"=="" (
    REM No argument provided, use the script's directory
    set "source_dir=%~dp0"
) else (
    REM Use the provided directory
    set "source_dir=%~1"
)

REM Initialize the base emcc command
set "emcc_command=emcc"

REM Use delayed expansion for appending file paths
setlocal enabledelayedexpansion

REM Find all .cpp files in the specified directory and subdirectories
for /r "%source_dir%" %%f in (*.cpp) do (
    REM Convert Windows-style backslashes to forward slashes
    set "file_path=%%~f"
    set "file_path=!file_path:\=/%!"
    set "emcc_command=!emcc_command! ^ "!file_path!""
)

REM Add other necessary options
set "emcc_command=!emcc_command! -std=c++23 -o index.html ^"
set "emcc_command=!emcc_command! -s USE_GLFW=3 -s FULL_ES2=1 -s USE_WEBGL2=1 -s WASM=1 ^"
set "emcc_command=!emcc_command! --emrun -gsource-map"

REM Output the generated command for verification
echo !emcc_command!

REM Optional: Execute the generated command
call !emcc_command!

endlocal

emrun index.html
