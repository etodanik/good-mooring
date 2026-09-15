# The-Forge Raytracing test

*IMPORTANT: Make sure you have build the release 16_Raytracing of each testable executable*

This Raytracing test applies to the following Raytracing executables:

* Windows x64 DirectX - The-Forge\Examples_3\Unit_Tests\PC_VS2019\x64\Release\16_Raytracing

* Linux	Vulkan - The-Forge\Examples_3\Unit_Tests\SteamOS_Codelite\16_Raytracing\Release\16_Raytracing


## Quick run

The script files will run the executable in full screen mode and close after 512 iterations, the output can be found in the same directory:

* RunWindowsDx.bat

* RunLinuxVk.sh


## Commandline arguments

The following commandline arguments are available:

* "-w [number]": set width of the screen

* "-h [number]": set height of the screen

* "-b [optional number]": turn on benchmark mode, optional number indicates the number of iterations [32, 512]

* "-f": turn on full screen mode

* "-o "../location"": output location name for profile file

Example: 

* `cd The-Forge\Examples_3\Unit_Tests\PC_VS2019\x64\Release\16_Raytracing`
* `mkdir raytracer`
* `16_Raytracing.exe -w 1280 -h 720 -b 512 -o "raytracer\myOutput"`

The profile results will be in
`raytracer\myOutputProfile-*.html`


## Shortcut keys

* Alt + Enter: toggle full screen
* F1: Hide/Show UI
* F3: Quick profile dump
* Space: Reset camera position
* Esc: Exit application


## Benchmark mode

The benchmark mode will automatically shutdown the program after two times a number of iterations (64) default.
On shutdown it will store a "{name}Profile-{timestamp}.html" file inside the executable folder (or output folder given through commandline), which contains the benchmark results.


## Linux

If permission is denied to execute the program, then you could use:

* chmod u+x program_name

This will make the program executable for the current user.