; RUN: rm -rf %t && mkdir -p %t/src/prebuilt %t/other/prebuilt
; RUN: cp %S/Inputs/prebuilt/dep.sld %S/Inputs/prebuilt/lib.sld %t/src/prebuilt/
; RUN: cp %S/Inputs/prebuilt/lib.sld %t/other/prebuilt/

;; Build both libraries from source and write their prebuilt modules.
; RUN: schir-scheme -I %schir_module_path -I %t/src \
; RUN:   --prebuild-module-path=%t/bc %s \
; RUN:   | FileCheck --check-prefixes=DEP,LIB,USE %s
; RUN: ls %t/bc | FileCheck --check-prefix=FILES %s

;; Use the prebuilt modules.
; RUN: schir-scheme -I %schir_module_path -I %t/src \
; RUN:   --prebuild-module-path=%t/bc %s \
; RUN:   | FileCheck --check-prefix=USE --implicit-check-not=source %s

;; Rebuild a library when a library it imports changes.
; RUN: touch %t/src/prebuilt/dep.sld
; RUN: schir-scheme -I %schir_module_path -I %t/src \
; RUN:   --prebuild-module-path=%t/bc %s \
; RUN:   | FileCheck --check-prefixes=DEP,LIB,USE %s

;; Do not use or overwrite a stale prebuilt module.
; RUN: touch %t/src/prebuilt/lib.sld
; RUN: schir-scheme -I %schir_module_path -I %t/src \
; RUN:   --prebuild-module-path=%t/bc --no-prebuild-module %s \
; RUN:   | FileCheck --check-prefixes=LIB,USE --implicit-check-not='dep from' %s
; RUN: schir-scheme -I %schir_module_path -I %t/src \
; RUN:   --prebuild-module-path=%t/bc --no-prebuild-module %s \
; RUN:   | FileCheck --check-prefixes=LIB,USE --implicit-check-not='dep from' %s

;; Rebuild a library found at a different source path.
; RUN: schir-scheme -I %schir_module_path -I %t/other -I %t/src \
; RUN:   --prebuild-module-path=%t/bc %s \
; RUN:   | FileCheck --check-prefixes=LIB,USE --implicit-check-not='dep from' %s

;; Use the prebuilt modules when the sources are missing.
; RUN: rm -rf %t/src %t/other
; RUN: schir-scheme -I %schir_module_path \
; RUN:   --prebuild-module-path=%t/bc %s \
; RUN:   | FileCheck --check-prefix=USE --implicit-check-not=source %s

;; Load module files explicitly.
; RUN: schir-scheme -I %schir_module_path \
; RUN:   --load-module=%t/bc/_SCHIRL8SprebuiltL3Sdep.sld.bc \
; RUN:   --load-module=%t/bc/_SCHIRL8SprebuiltL3Slib.sld.bc %s \
; RUN:   | FileCheck --check-prefix=USE --implicit-check-not=source %s

; FILES: _SCHIRL8SprebuiltL3Sdep.sld.bc
; FILES: _SCHIRL8SprebuiltL3Slib.sld.bc

; DEP: "dep from source"
; LIB: "lib from source"
; USE: (lib (dep 1))
(import (schir base)
        (prebuilt lib))
(write (lib-list 1))
(newline)
