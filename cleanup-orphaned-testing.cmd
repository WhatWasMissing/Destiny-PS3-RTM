@echo off
setlocal
pushd "%~dp0" || exit /b 1

echo This command removes only generated Python bytecode and old search manifests:
echo   tests\__pycache__\test_elfaudit.cpython-314.pyc
echo   tools\__pycache__\rtm_elf_audit.cpython-314.pyc
echo   source-search-paths.txt
echo   go-project-paths.txt
echo   source-content-matches.txt
echo.
choice /c YN /m "Delete these exact generated files"
if errorlevel 2 goto :cancel

if exist "tests\__pycache__\test_elfaudit.cpython-314.pyc" del /q "tests\__pycache__\test_elfaudit.cpython-314.pyc"
if exist "tools\__pycache__\rtm_elf_audit.cpython-314.pyc" del /q "tools\__pycache__\rtm_elf_audit.cpython-314.pyc"
if exist "source-search-paths.txt" del /q "source-search-paths.txt"
if exist "go-project-paths.txt" del /q "go-project-paths.txt"
if exist "source-content-matches.txt" del /q "source-content-matches.txt"

rem Remove either cache directory only if it is empty after deleting the exact file above.
if exist "tests\__pycache__\" rd "tests\__pycache__" 2>nul
if exist "tools\__pycache__\" rd "tools\__pycache__" 2>nul

echo Cleanup complete.
goto :done

:cancel
echo Cancelled. No files were deleted.

:done
popd
endlocal
