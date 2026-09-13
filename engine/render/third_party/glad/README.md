# glad — OpenGL loader (vendored, pre-generated)

Files here were generated with [glad2](https://github.com/Dav1dde/glad) and
committed (not generated at build time):

- generator: glad2 v2.0.8 (`pip install glad2`), `C` generator
- spec: Khronos `gl.xml` (OpenGL-Registry), API `gl:core=4.1`, **no extensions**
  (macOS tops out at GL 4.1 core; see docs/03 §4)
- files: `include/glad/gl.h`, `include/KHR/khrplatform.h`, `src/gl.c`

License:

- glad (the generator): MIT — https://github.com/Dav1dde/glad/blob/glad2/LICENSE
- generated loader code: WTFPL OR CC0-1.0 (glad's generator output terms)
- `KHR/khrplatform.h`: Apache-2.0 / MIT dual (Khronos Group)

Do not edit the generated files by hand; regenerate with the command above and
re-vendor.
