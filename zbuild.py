import subprocess
import os
import glob

PROJECT_ROOT = "/home/rei/Projects/media_player"

EXECUTABLE = [PROJECT_ROOT + "/build/mediaplayer"]

SRC = glob.glob(PROJECT_ROOT + "/**/*.cpp", recursive=True)
C_SRC = glob.glob(PROJECT_ROOT + "/**/*.c", recursive=True)

COMPILE_FLAGS = [
    "-I", PROJECT_ROOT + "/include",
    "-L", PROJECT_ROOT + "/build",
    "-g"
]

LINK_FLAGS = ["-lglfw", "-lGL", "-lslp_png", "-Wl,-rpath," + PROJECT_ROOT + "/build"]

CC = ["g++"]
C_CC = ["gcc"]

def compile():
    try:
        os.makedirs(PROJECT_ROOT + "/build", exist_ok=True)
        os.chdir(PROJECT_ROOT + "/build")
        print("Compiling...")

        print(f"src: {SRC}")
        result = subprocess.run(CC + ["-c"] + SRC + COMPILE_FLAGS, capture_output=True, text=True)
        if result.stdout.__len__() != 0:
            print(result.stdout)
        if result.returncode != 0:
            print(result.stderr)
            exit(0)

        print(f"src: {C_SRC}")
        result = subprocess.run(C_CC + ["-c"] + C_SRC + COMPILE_FLAGS, capture_output=True, text=True)
        if result.stdout.__len__() != 0:
            print(result.stdout)
        if result.returncode != 0:
            print(result.stderr)
            exit(0)

        obj_files = glob.glob(PROJECT_ROOT + "/build/*.o", recursive=False)

        print(f"obj: {obj_files}")
        subprocess.run(["cp", PROJECT_ROOT + "/include/slp_png/libslp_png.so", PROJECT_ROOT + "/build"])
        result = subprocess.run(CC + ["-o"] + EXECUTABLE + obj_files + COMPILE_FLAGS + LINK_FLAGS, capture_output=True, text=True)
        if result.stdout.__len__() != 0:
            print(result.stdout)
        if result.returncode != 0:
            print(result.stderr)
            exit(0)

        for obj in obj_files:
            os.remove(obj)
        
        print("Success")
    except Exception as e:
        print(e)

if __name__ == "__main__":
    compile()
