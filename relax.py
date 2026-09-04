# Enable AVR linker relaxation: call/jmp (4 B) -> rcall/rjmp (2 B) wherever the target is
# in range. Pure encoding change, no semantic effect; the binary was carrying 565 `call`
# against 13 `rcall`. PlatformIO does not pass -mrelax from build_flags to the link step,
# so it has to be appended to LINKFLAGS here.
Import("env")
env.Append(LINKFLAGS=["-Wl,--relax"])
