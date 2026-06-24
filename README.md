# Schir Project

This repository is an umbrella project for the Schir R7RS Scheme compiler,
the Geomalg compiler for Geometric Algrebra,
and the Nbdl state managment library.
Each of these subprojects are implemented using C++ and MLIR each with their
own dialect.

## Schir

Schir is an almost complete implementation of R7RS Scheme that compiles
using the MLIR compiler infrastructure as well as provides libraries for
generating MLIR operations. It's focus is on fast scripting to
be used as a frontend for both domain specific compilers as well as a tool
for metaprogramming in Clang by embedding Scheme as a compile-time
scripting language via a pragma entrypoint.

Currently, Schir relies on a minimal [fork of the LLVM Project](
https://github.com/ricejasonf/llvm-project/tree/ricejasonf/parser-pragma)
that extends the Clang plugin system to enable pragma entrypoints with more
control over the lexer and the token stream as well as the adding ability to
evaluate constant expressions making results available to the embedded runtime.

## Geomalg

Geomalg is an experimental domain specific compiler with operations modelling
the Geometric Algebra. It implements a MLIR Dialect to allow expansion of
operations from the algebra and provides support for lowering to both LLVM
and SPIRV for CPU and GPU targets. The project is made to be highly extensible
with how operations are expanded, but the primary usage involves 5D Conformal
Geometric Algebra using the left contraction as the inner product. This is
heavily inspired by the book
[*Geometric Algebra for Computer Science (GA4CS)*](https://geometricalgebra.org)
by Dorst, Fontijne, and Mann. (2007).

Currently, the focus is to implement interpolation of rigid body motion that can
run deterministic lockstep simulations on the CPU as well as running additional
GPU computations via shader programs.

## Nbdl

Nbdl is an experimental state management library that began its life using
C++ metaprogramming back in 2015. Slow build times and tedious interfaces
motiviated the creation of Schir to serve as a frontend to a Nbdl MLIR Dialect
that generates and injects C++ into Clang with the original library's hooks.

## Building the Project
Building the project requires CMake, LLVM, Clang, and MLIR.
A release build of the required [fork of the LLVM Project](
https://github.com/ricejasonf/llvm-project/tree/ricejasonf/parser-pragma)
can be found on DockerHub [ricejasonf/llvm-project](
https://hub.docker.com/repository/docker/ricejasonf/llvm-project/general).
From this image one can copy `/opt/install` to a preferred installation
directory. Currently, the project is only tested on Linux.

To build the LLVM fork, use the typical out of source build directory.
Then Schir can be built with `cmake` using the directive
`-DSCHIR_LLVM_ROOT=llvm_build_dir` that points to the root path
of the LLVM build directory.

It is important to note that Schir makes use of dynamically loaded
shared libraries so be sure to link against the same libraries
that the project was built with.

A good starting point could be
```
make check-schir -j8
```
or 
```
make check-geomalg -j8
```

If you want to build Nbdl, you may want to look at the
[CppDock](https://github.com/ricejasonf/cppdock) project
that handles building Docker images with the required
depencencies.
