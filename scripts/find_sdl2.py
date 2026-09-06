import os
Import("env")

CANDIDATES = [
    os.environ.get("MACPORTS_PREFIX"),
    os.environ.get("HOMEBREW_PREFIX"),
    "/opt/local",
    "/opt/homebrew",
    "/usr/local",
]

for c in CANDIDATES:
    if not c:
        continue
    inc = os.path.join(c, "include", "SDL2")
    if os.path.isfile(os.path.join(inc, "SDL.h")):
        env.Append(CCFLAGS=[
            "-I" + os.path.join(c, "include"),
            "-I" + inc,
        ])
        env.Append(LINKFLAGS=[
            "-L" + os.path.join(c, "lib"),
            "-lSDL2",
        ])
        print("find_sdl2: using SDL2 from " + c)
        break
else:
    print("find_sdl2: WARNING - SDL2 not found; install via MacPorts (`port install sdl2`) or Homebrew (`brew install sdl2`)")
