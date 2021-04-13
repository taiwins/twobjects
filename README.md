## Taiwins wayland objects library

The twobject library is merged back inside libtaiwins. This library is no longer 
needed. Objects lives under `libtaiwins/objects`.

This library implements basic wayland protocols as well as various other wayland
protocols like 'xdg-shell' and 'linux-dmabuf'. The library is designed to be
backend agnostic and autonomous. That is, it does not depend on a wayland
backend to work. It also exposes as few APIs as possible for easy intergration
into a compositor. 

### Building
* meson
* wayland
* wayland-protocols
* xkbcommon
* libdrm (only for a header file)

Run the commands:
```
meson build
ninja -C build
```
### License
Nearly All the code are licensed under GPLv2 license expect `os-compatibility.c`
and `os-compatibility.h` is released under MIT license.
