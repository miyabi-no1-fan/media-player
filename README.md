# media-player

## Support platforms
- OS: Linux.
- Architecture: x86_64.

Other platforms might still work, but never tested.
Open an issue if you want support on your platform.

## Dependencies
- Vulkan: 1.0 and `vulkan/vulkan.hpp`
- GLFW: 3.2 or newer
- RtAudio: 6.0 or newer
- FFmpeg


## Install
```bash
git clone https://github.com/miyabi-no1-fan/media-player.git
cd media-player
./build.py # change some option in the file if you want
```
Note: media-player depends on shaders/dist/*.spv with hardcoded file path.
If you want shaders to be embed to the binary, create an issue.
