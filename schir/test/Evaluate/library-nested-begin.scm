; RUN: schir-scheme --module-path=%schir_module_path --module-path=%S/Inputs %s | FileCheck %s
(import (schir base) (my nested-begin))

; CHECK: (1 2 3)
(write (list x y z))
(newline)
