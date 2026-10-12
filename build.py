#! /bin/python3

import subprocess
from pathlib import Path
import shlex
import os

CC = "clang++"

LIBRARIES = []

# options: "Debug" "Release"
BUILD_TYPE = "Debug"

FLAGS = "-Wall -Werror -Wextra -Wno-unused"

if BUILD_TYPE == "Release":
    FLAGS += " -DNDEBUG -O2 -march=native -mtune=native"
    LTO = "ON"
elif BUILD_TYPE == "Debug":
    FLAGS += " -g -Og"
    LTO = "OFF"

THREADS = 12

BUILD_DIR = Path("build")

SHADER_COMPILER = "glslc"
SHADER_SRC_DIR = Path("shaders")
SHADER_BUILD_DIR = Path("shaders/dist")

def run(cmd: str):
    print(cmd)
    subprocess.run(shlex.split(cmd))

def main():
    run(f"cmake -B {BUILD_DIR} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_COMPILER={CC} -DCMAKE_CXX_FLAGS=\"{FLAGS}\" -DCMAKE_BUILD_TYPE={BUILD_TYPE} -DLTO={LTO}")
    run(f"cmake --build {BUILD_DIR} --config {BUILD_TYPE} -j{THREADS}")

    for shader in SHADER_SRC_DIR.rglob('*'):
        if shader.is_file() and not SHADER_BUILD_DIR in shader.parents:
            output = SHADER_BUILD_DIR / shader.relative_to(SHADER_SRC_DIR)
            output.parent.mkdir(parents=True, exist_ok=True)
            run(f"{SHADER_COMPILER} {shader} -o {output}.spv")
    print("===============================================================================")

if __name__ == "__main__":
    main()