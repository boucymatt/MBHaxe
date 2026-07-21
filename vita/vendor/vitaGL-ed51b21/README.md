# Pinned, patched vitaGL

This is the complete source for vitaGL commit
`ed51b21c2882724e81ec77dc053a7927266922c6`.

Upstream: <https://github.com/Rinnegatamante/vitaGL>

The revision includes vitaGL's framebuffer/texture coherency repair. One local
change gives the GLSL preprocessor output buffer the same 1 MiB headroom that
the rest of the translator already expects:

```c
char *out = vglMalloc(source_size + MEM_ENLARGER_SIZE);
```

The upstream code allocated only `strlen(input)` bytes even though the
preprocessor writes a terminating NUL and can expand macros. CMake builds this
source as one static library so every private structure and feature definition
comes from the same revision. The source remains covered by upstream's LGPLv3
license in `COPYING.LESSER`.
