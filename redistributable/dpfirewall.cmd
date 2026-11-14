@echo off
setlocal

:main
    set "_action="
    if /i "%~1"=="allow"  set "_action=allow"
    if /i "%~1"=="revoke" set "_action=revoke"
    if "%_action%"=="" goto err_usage

    if "%~2"=="" goto err_usage
    set "_program=%~f2"
    set "_rule=DirectPlay: %_program%"

    goto do_%_action%

:err_usage
    echo Usage:
    echo     %~nx0 allow^|revoke "C:\Path\To\Game.exe"
    echo.
    echo Idempotently configures an inbound Windows Firewall rule that allows the
    echo application on every port and protocol, on private and domain networks.
    echo A rule that blocks the application still takes precedence, such as the
    echo one Windows stores when its firewall prompt is dismissed.
    echo.
    echo Requires administrator permissions.
    echo.
    exit /b 160

:do_revoke
    netsh advfirewall firewall show rule name="%_rule%" >nul 2>&1 || exit /b 0
    netsh advfirewall firewall delete rule name="%_rule%"
    exit /b

:do_allow
    if not exist "%_program%" (
        echo The system cannot find the file specified: "%_program%"
        exit /b 2
    )

    if exist "%_program%\" (
        echo The operation is not supported on a directory: "%_program%"
        exit /b 336
    )

    netsh advfirewall firewall delete rule name="%_rule%" >nul 2>&1
    netsh advfirewall firewall add rule name="%_rule%" dir=in action=allow program="%_program%" enable=yes profile=private,domain
    exit /b
