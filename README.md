
This is attempt to remind myself of chromium internals and to learn some new things, by writing small programs using chromium code as a typical library.

Master branch contains incremental results, different branches contain different
examples and different phases.

How to compile the code:
 * Consult [chromium docs](https://source.chromium.org/chromium/chromium/src/+/main:docs/README.md) on how to get and compile the code
 * Clone this repository into the root `src` directory of chromium
 * Apply patch to add target to BUILD.gn `git apply chromiumexamples/buildgn.patch`
 * Build target chromiumexamples, e.g. `autoninja -C out/Default chromiumexamples`


