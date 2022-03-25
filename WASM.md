# Bugdom for the Web

***CURRENT STATUS: LAUNCHES, BEGINS TO INITIALIZE, CRASHES ALMOST IMMEDIATELY***

This fork contains the in-progress port of Bugdom for the web, targetting [WASM](https://webassembly.org/) using [Emscripten](https://emscripten.org/).

## TL;DR

To build run the following:

```bash
rm -rf build lib # Clean any build that may exist
/usr/lib/emscripten/emcmake cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug # Use Emscripten to Configure
cmake --build build --verbose # Build with CMake
```

From there you'll have the web assets in `build/`. Running `build/Bugdom.html` directly will not work due to CORS, using a web server will be necesary.

Using the [`serve`](https://www.npmjs.com/package/serve) npm package will do this quickly:

```bash
npx -y serve build
```

## Dependencies

In order to build this for you will need Emscripten installed on your system. I used the `emscripten` package from the Arch repository, please refer to the emscripten documentation for [installation](https://emscripten.org/docs/getting_started/downloads.html) for your system.

A port of [SDL2 is built into Emscripten](https://emscripten.org/docs/compiling/Building-Projects.html?highlight=sdl2#emscripten-ports).

Additonally, this fork adds a dependency for [gl4es](https://github.com/ptitSeb/gl4es) at the recommendation of the emscription documentation for [using OpenGL](https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html?highlight=opengl#what-if-my-codebase-depends-on-a-desktop-opengl-feature-that-is-currently-unsupported). This is handled through a Git submodule similarly to the existing Pomme dependency. Becuase we can't easilly target OpenGL 2 on the browser, we use GL4ES to bridge this older API to the web-friendly OpenGL ES 2.

## Game Assets

Emscripten is able to handle the bundling and handling of the assets needed to run the game, using its [virtual file system](https://emscripten.org/docs/porting/files/packaging_files.html?highlight=preload#packaging-files). This is configured in this fork's CMake.

## External Patches Needed

In its current configuration, gl4es is not providing all of the symbols (unprefixed) this project needs when working with the GL context in the renderer. [A patch to fix this is available](https://github.com/ptitSeb/gl4es/issues/364#issuecomment-991710051).

## Prior Art

- [Compiling an SDL2 Game to WASM](https://dev.to/mattconn/compiling-an-sdl2-game-to-wasm-42fj)