#! /bin/python3

import subprocess
from pathlib import Path
import shlex
import os

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

BUILD_DIR = Path("build")

SHADER_COMPILER = "glslc"
SHADER_SRC_DIR = Path("shaders")
SHADER_BUILD_DIR = Path("shaders/dist")

def run(cmd: str):
    print(cmd)
    ret = subprocess.run(shlex.split(cmd))
    if ret.returncode != 0:
        exit(ret.returncode)

def main():
    for shader in SHADER_SRC_DIR.rglob('*'):
        if shader.is_file() and not SHADER_BUILD_DIR in shader.parents:
            output = SHADER_BUILD_DIR / shader.relative_to(SHADER_SRC_DIR)
            output.parent.mkdir(parents=True, exist_ok=True)
            run(f"{SHADER_COMPILER} {shader} -o {output}.spv")
    run(f"cmake -B {BUILD_DIR} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_COMPILER={CC} -DCMAKE_CXX_FLAGS=\"{FLAGS}\" -DCMAKE_BUILD_TYPE={BUILD_TYPE} -DLTO={LTO}")
    run(f"cmake --build {BUILD_DIR} --config {BUILD_TYPE} -j{os.process_cpu_count()}")
    print("===============================================================================")

if __name__ == "__main__":
    main()