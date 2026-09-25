#include "PassRegistration.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>

extern "C" int
optMain(int argc, char **argv,
        llvm::ArrayRef<std::function<void(llvm::PassBuilder &)>> passBuilderCallbacks);

namespace {

int expandArguments(int argc, char **argv) {
  if (argc < 3 || (llvm::StringRef(argv[2]) != "posix" && llvm::StringRef(argv[2]) != "windows")) {
    llvm::errs() << "a2mba-opt: expected response-file quoting mode: posix or windows\n";
    return 2;
  }

  bool windowsQuoting = llvm::StringRef(argv[2]) == "windows";
  llvm::SmallVector<const char *, 32> arguments(argv + 3, argv + argc);
  for (llvm::StringRef argument : arguments) {
    if (argument == "--rsp-quoting=windows") {
      windowsQuoting = true;
    } else if (argument == "--rsp-quoting=posix") {
      windowsQuoting = false;
    }
  }

  llvm::BumpPtrAllocator allocator;
  llvm::cl::ExpansionContext expansion(allocator, windowsQuoting
                                                      ? llvm::cl::TokenizeWindowsCommandLine
                                                      : llvm::cl::TokenizeGNUCommandLine);
  if (llvm::Error error = expansion.expandResponseFiles(arguments)) {
    llvm::errs() << "a2mba-opt: " << llvm::toString(std::move(error)) << '\n';
    return 2;
  }

  llvm::json::Array expanded;
  for (llvm::StringRef argument : arguments) {
    expanded.emplace_back(argument);
  }
  llvm::json::OStream output(llvm::outs());
  output.value(std::move(expanded));
  llvm::outs() << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && llvm::StringRef(argv[1]) == "--a2mba-expand-args") {
    llvm::InitLLVM initialization(argc, argv);
    return expandArguments(argc, argv);
  }
  std::function<void(llvm::PassBuilder &)> callback = a2mba::registerPasses;
  return optMain(argc, argv, {callback});
}
