The compiled shaders of [libvita2d](https://github.com/xerpi/libvita2d), under its MIT license, as Vita3K's `tools/native-tool` also carries them. The build embeds them in the test app, see `CMakeLists.txt`, and `render.c` draws with them.

- `clear_v`, `clear_f`: `aPosition` to the position, and the color `uClearColor`.
- `color_v`, `color_f`: `aPosition` times `wvp`, and the color `aColor`.
- `texture_v`, `texture_f`: `aPosition` times `wvp`, and `aTexcoord` to sample `tex`.
