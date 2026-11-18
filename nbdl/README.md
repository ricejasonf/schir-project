# Nbdl

>Network Based Data Layer
>
>C++ Application State Management Library

## Overview

This project is a work in progress.

Nbdl specifications are written in Scheme, embedded in C++ with
`#pragma schir_scheme`, and compiled through the Nbdl MLIR dialect by the
Schir Clang plugin. This requires Clang from the
[LLVM fork](https://github.com/ricejasonf/llvm-project/tree/ricejasonf/parser-pragma)
that Schir is built against, which also targets WebAssembly through
Emscripten. _(see the Build section below)_

## Basic Features

* Context object for managing state, network connection, and message dispatching
* Concepts for wrapping server/client implementations and Stores
* Concepts for generic recursive visitation of arbitrary data structures
* Concepts for serialization of Boost.Hana and user-defined data types
  * API hash versioning
* Promise interface for chaining events
* A variant type optimized for use on a single thread, with a safe interface,
  and most importantly it plays well with the serialization facilities
  which is used for dispatching messages and representing state.

## Features for Web Frontends

* Rendering state on a web frontend via DOM manipulations. (Requires Emscripten)
  * Navigation/Location is available as state
* Server/Client websocket integration and Browser Client Websocket API integration

## Features Coming Soon

* Database persistence integration
* Multi-platform UI toolkit
* Documentation

## Build the Development Environment

Nbdl uses [CppDock](https://github.com/ricejasonf/cppdock) with podman (or Docker) to create a development environment with all of the dependencies installed, including the `ricejasonf/schir-project` image.

See the `cppdock.json` file for the platforms and their dependencies:

- `develop` builds for Linux x64.
- `web_develop` builds to WebAssembly with Emscripten and runs the tests with node.

To build and run the tests:

```
cppdock dev develop --target check
cppdock dev web_develop --target check
```

To open a shell in the container instead, use `cppdock dev develop`.

To build against a local Schir build instead of the locked image, see
"Building with CppDock" in the README of the parent project.
