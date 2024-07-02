#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>  // TODO: Unordered map?
using namespace llvm;

namespace {

std::map<std::string, int> allocaCounts;

struct InstructionCombiningPass : public PassInfoMixin<InstructionCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {

        for (auto &F : M) {
            for (auto &BB : F) {
                for (auto &I : BB) {
      
                    }
                }
            }
        return PreservedAnalyses::all();  // TODO: Change this if we go back to the old pass manager
        }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        .APIVersion = LLVM_PLUGIN_API_VERSION,
        .PluginName = "InstructionCombiningPass",
        .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                    MPM.addPass(InstructionCombiningPass());
                });
        }
    };
}
