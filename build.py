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

def run(cmd: str):
    print(cmd)
    subprocess.run(shlex.split(cmd))

def main():
    run(f"cmake -B {BUILD_DIR} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_COMPILER={CC} -DCMAKE_CXX_FLAGS=\"{FLAGS}\" -DCMAKE_BUILD_TYPE={BUILD_TYPE} -DLTO={LTO}")
    run(f"cmake --build {BUILD_DIR} --config {BUILD_TYPE} -j{THREADS}")

if __name__ == "__main__":
    main()