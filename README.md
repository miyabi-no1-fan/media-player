# media-player

## Support platforms
- OS: Linux
- Architecture: x86_64

Other platforms might still work, but never tested.
Open an issue if you want support on your platform.

## Dependencies
- Vulkan
- FFmpeg

## Install
We have pre-built binaries in releases.
Just make sure that you have installed FFmpeg then it should work.

If you build from source:
- Make sure you have installed Vulkan SDK 1.4.350 or newer
- We only support `clang++` and `g++` compiler
- If you're on Windows, make sure you have installed the Ninja generator

```bash
git clone https://github.com/miyabi-no1-fan/media-player.git
cd media-player
python build.py
```
`build.py` is just a small script that call `glslc` and `cmake`,
you can modify it as you like.
