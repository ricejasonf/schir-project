// RUN: clang++ -std=c++26 -I %schir_module_path -I %S/Inputs -fsyntax-only -fplugin=SchirClang.so -fpass-plugin=SchirLLVMPass.so -Xclang -verify %s
// expected-no-diagnostics

// TODO something.
