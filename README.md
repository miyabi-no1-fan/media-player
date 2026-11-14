# media-player

## Support platforms
We _think_ that it should still gonna work on most Linux and Windows, but we never test all of them so we can't guarantee that it's gonna work for your case.

You can open an issue if you want it to work for your case, we'll make it work for ya.

## Dependencies
- Vulkan
- FFmpeg

## Install
We have pre-built binaries in releases.
Just make sure that you have installed FFmpeg then it should work.

If you build from source:
- Make sure you have installed the Ninja generator and Vulkan SDK 1.4.350 or newer
- We only support `clang++` and `g++` compiler

```bash
git clone https://github.com/miyabi-no1-fan/media-player.git
cd media-player
cmake --preset release
cmake --build --preset release
```
