@echo off
set cwd=%cd%
IF "%cwd:~-9%" == "The-Forge" (
    Tools\python-3.6.0-embed-amd64\python.exe Common\Tools\ReloadServer\ReloadServer.py --kill
    Tools\python-3.6.0-embed-amd64\python.exe Common\Tools\ReloadServer\ReloadServer.py %*
) ELSE (
    if "%cwd:~-12%" == "ReloadServer" (
        ..\..\..\Tools\python-3.6.0-embed-amd64\python.exe ReloadServer.py --kill
        ..\..\..\Tools\python-3.6.0-embed-amd64\python.exe ReloadServer.py %*
    ) 
ELSE (
    if "%cwd:~-14%" == "ParticleEditor" (
        ..\..\..\..\..\..\The-Forge\Tools\python-3.6.0-embed-amd64\python.exe ..\..\..\..\..\..\The-Forge\Common\Tools\ReloadServer\ReloadServer.py --kill
        ..\..\..\..\..\..\The-Forge\Tools\python-3.6.0-embed-amd64\python.exe ..\..\..\..\..\..\The-Forge\Common\Tools\ReloadServer\ReloadServer.py %*
    ) ELSE (
        echo "ERROR: You must execute the daemon script from The-Forge root directory or by double clicking: .\\Common\\Tools\\ReloadServer\\ReloadServer.bat"
    )
)