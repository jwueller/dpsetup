@echo off
setlocal

:main
    if /i "%~1"=="add"      goto update
    if /i "%~1"=="delete"   goto update
    goto err_usage

:update
    if "%~2"=="" goto err_usage
    set "_rule=DirectPlay: %~nx2"

    netsh advfirewall firewall delete rule name="%_rule%" dir=in >nul 2>&1
    if /i "%~1"=="delete" (
        netsh advfirewall firewall show rule name="%_rule%" dir=in >nul 2>&1 && exit /b 1
        exit /b 0
    )

    netsh advfirewall firewall add rule name="%_rule%" dir=in action=allow program="%~f2" enable=yes profile=private,domain >nul 2>&1
    exit /b %errorlevel%

:err_usage
    echo Usage:
    echo     %~nx0 add^|delete "C:\Path\To\Game.exe"
    echo.
    echo Configures an inbound Windows Firewall rule the same way Windows would
    echo after prompting the user when the application listens for the first
    echo time: For every port, TCP and UDP, on private and domain networks.
    echo.
    echo Requires administrator permissions.
    exit /b 161
