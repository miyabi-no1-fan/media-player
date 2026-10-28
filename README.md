# media-player

## Support platforms
- OS: Linux.
- Architecture: x86_64.

Other platforms might still work, but never tested.
Open an issue if you want support on your platform.

## Dependencies
- Vulkan
- FFmpeg


## Install
```bash
git clone https://github.com/miyabi-no1-fan/media-player.git
cd media-player
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
The output binary is `build/media-player`.
Also keep in mind that dependencies are dynamically linked.
