#include <schir/Context.h>
#include <schir/Value.h>

extern "C" {
schir::ContextLocal schir_hello_world_ultimate_answer;

// This is only used to test the binding.
void schir_hello_world_get_ultimate_answer(schir::Context& C,
                                           schir::ValueRefs) {
  schir::Value V = schir_hello_world_ultimate_answer.get(C);
  C.Cont(V);
}

void schir_hello_world_my_write(schir::Context& C,
                                schir::ValueRefs Args) {
  schir::write(llvm::outs(), Args[0]);
  C.Cont();
}
}
