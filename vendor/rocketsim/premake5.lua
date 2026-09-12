project "RocketSim"
    language "C++"
    kind "StaticLib"
    cppdialect "C++20"
    targetdir(buildpath("mta"))
    warnings "Off"
    defines { "RS_DONT_LOG" }
    floatingpoint "Strict"
    files { "src/**.cpp", "src/**.h", "libsrc/**.cpp", "libsrc/**.h", "premake5.lua", "LICENSE" }
    filter "architecture:not x86"
        flags { "ExcludeFromBuild" }
    filter "system:not windows"
        flags { "ExcludeFromBuild" }

