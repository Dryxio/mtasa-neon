@echo off
setlocal
set "NEON_PYTHON_SCRIPT=%~dp0Tools\neon-api\neon.py"

py -3 -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
if not errorlevel 1 goto neon_run_py_launcher

call python3 -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
if not errorlevel 1 goto neon_run_python3

call python -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
if not errorlevel 1 goto neon_run_python

for /d %%D in ("%LocalAppData%\Programs\Python\Python*") do (
    if exist "%%~fD\python.exe" (
        "%%~fD\python.exe" -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
        if not errorlevel 1 (
            set "NEON_SELECTED_PYTHON=%%~fD\python.exe"
            goto neon_run_selected
        )
    )
)

for /d %%D in ("%ProgramFiles%\Python*") do (
    if exist "%%~fD\python.exe" (
        "%%~fD\python.exe" -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
        if not errorlevel 1 (
            set "NEON_SELECTED_PYTHON=%%~fD\python.exe"
            goto neon_run_selected
        )
    )
)

>&2 echo Neon CLI requires Python 3.10 or newer. Install a current Python 3 and try again.
exit /b 2

:neon_run_py_launcher
py -3 "%NEON_PYTHON_SCRIPT%" %*
exit /b %errorlevel%

:neon_run_python3
call python3 "%NEON_PYTHON_SCRIPT%" %*
exit /b %errorlevel%

:neon_run_python
call python "%NEON_PYTHON_SCRIPT%" %*
exit /b %errorlevel%

:neon_run_selected
"%NEON_SELECTED_PYTHON%" "%NEON_PYTHON_SCRIPT%" %*
exit /b %errorlevel%
