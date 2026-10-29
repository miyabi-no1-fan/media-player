#! /bin/python3

import subprocess
from pathlib import Path
import os
import platform

# options: "g++" "clang++"
CC = "clang++"

# options: "Debug" "Release"
BUILD_TYPE = "Release"

FLAGS = "-Wall -Wextra -Wpedantic -Werror -Wno-c23-extensions"

if BUILD_TYPE == "Release":
    FLAGS += " -DNDEBUG -O2 -march=x86-64 -mtune=x86-64"
    LTO = "ON"
elif BUILD_TYPE == "Debug":
    FLAGS += " -g -Og -fno-omit-frame-pointer -fsanitize=address,undefined"
    LTO = "OFF"

GENERATOR = None
if platform.system() == "Windows":
    # The default Visual Studio generator will force MSVC
    # -- MSVC compiler is not supported
    GENERATOR = "Ninja"

BUILD_DIR = Path("build")

SHADER_COMPILER = "glslc"
SHADER_SRC_DIR = Path("shaders")
SHADER_BUILD_DIR = Path("shaders/dist")

def run(cmd: list[str]):
    cmd = [str(arg) for arg in cmd]
    print(" ".join(cmd))
    ret = subprocess.run(cmd)
    if ret.returncode != 0:
        exit(ret.returncode)

def main():
    for shader in SHADER_SRC_DIR.rglob('*'):
        if shader.is_file() and SHADER_BUILD_DIR not in shader.parents:
            output = SHADER_BUILD_DIR / shader.relative_to(SHADER_SRC_DIR)
            output.parent.mkdir(parents=True, exist_ok=True)
            run([SHADER_COMPILER, shader, "-o", f"{output}.spv"])
    run([
        "cmake",
        "-B", BUILD_DIR,
        *(["-G", GENERATOR] if GENERATOR is not None else []),
        "-DCMAKE_CXX_COMPILER=" + CC,
        "-DFLAGS=" + FLAGS,
        "-DLTO=" + LTO,
        "-DCMAKE_BUILD_TYPE=" + BUILD_TYPE,
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    ])
    run([
        "cmake",
        "--build", BUILD_DIR,
        "--config", BUILD_TYPE,
        f"-j{os.process_cpu_count()}"
    ])
    print("===============================================================================")

if __name__ == "__main__":
    main()