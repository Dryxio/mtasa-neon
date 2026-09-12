project "RocketWebKernel"
    language "C"
    cdialect "C11"
    kind "StaticLib"
    targetdir(buildpath("mta"))
    warnings "Off"
    defines { "NEON_WASM_UNSHARED_ONLY", "WASM_RT_USE_MMAP=0", "WASM_RT_MEMCHECK_BOUNDS_CHECK=1" }
    floatingpoint "Strict"
    includedirs { "runtime", "." }
    files { "Kernel.c", "Kernel.h", "generated/**.c", "generated/**.h", "runtime/**.c", "runtime/**.h", "runtime/**.inc" }
    filter "system:not windows"
        pic "On"
        buildoptions { "-ffp-contract=off" }
    filter "system:linux"
        -- Strict C11 hides POSIX signal-aware jumps used by the wasm trap runtime.
        defines { "_POSIX_C_SOURCE=200809L" }
    filter {}
