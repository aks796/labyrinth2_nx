# portlibs32

The 32-bit (AArch32) Mesa, libGLESv1_CM and libdrm_nouveau the wrapper links
against: copy `include/` and `lib/` from
[mesa32](https://github.com/aks796/mesa32) (its `prefix/` after `./build.sh`,
or its release tarball) here. Without them
the Makefile builds the null renderer (the game runs, nothing is drawn).
