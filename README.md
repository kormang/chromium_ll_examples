
# Chromium low level internals example

This is attempt to remind myself of chromium internals and to learn some new
things, by writing small programs using chromium code as a typical library.

Code could be much cleaner, sorry!

## Objectives

  1. Write simple downloader that downloads contents of URL and prints them to
    console, using low level components from `//net` and `//base` to start
    IO thread and issue download on IO thread.
  2. Issue download, not using only task posting API, but mojo pipes. Still
    single process.
  3. Start child process and send request for download over mojo pipes.

So, the main objectives are going through task and threading API, Mojo, and
multiprocess communication using low level primitives from `//base` and `mojo`.
Additionally, use relatively low level component from `//net` to download
contents.

Master branch contains incremental results, different branches contain different
examples at different phases.

## How to compile the code

 * Consult [chromium docs](https://source.chromium.org/chromium/chromium/src/+/main:docs/README.md) on how to get and compile the code
 * Clone this repository into the root `src` directory of chromium
 * Apply patch to add target to BUILD.gn
  `git apply chromium_ll_examples/buildgn.patch`
 * Build target chromium_ll_examples, e.g.
  `autoninja -C out/Default chromium_ll_examples`
