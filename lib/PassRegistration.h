#pragma once

namespace llvm {
class PassBuilder;
}

namespace a2mba {

void registerPasses(llvm::PassBuilder &passBuilder);

} // namespace a2mba
