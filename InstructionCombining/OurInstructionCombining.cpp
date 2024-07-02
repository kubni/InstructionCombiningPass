#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>  // TODO: Unordered map?
using namespace llvm;

namespace {
std::vector<Instruction *> InstructionsToRemove;
std::map<Value*, Value*> ValuesMap;
// std::map<int, int> addInstrCount;

int addInstrCount = 0;

struct AddInstrCountPass : public PassInfoMixin<AddInstrCountPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            for (auto &BB : F) {
                for(auto &I : BB) {
                    if(isa<AddOperator>(I)) {
                        addInstrCount++;
                    }   
                }
            }
        }
        return PreservedAnalyses::all();
    }
};

struct InstructionCombiningPass : public PassInfoMixin<InstructionCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {

        for (auto &F : M) {
            for (auto &BB : F) {
                for (auto &I : BB) {
                    if(LoadInst *LoadInstr  = dyn_cast<LoadInst>(&I)) {
                        ValuesMap[LoadInstr] = LoadInstr->getOperand(0);
                    }
                    if (BinaryOperator *BinaryOp = dyn_cast<BinaryOperator>(&I)) {

                    }
                    // errs() << "Inst: " <<  I.getnam << "\n";
                        //   %1 = alloca i32, align 4
                        //   %2 = alloca i32, align 4
                        //   %3 = alloca i32, align 4
                        //   %4 = alloca i32, align 4
                        //   store i32 0, i32* %1, align 4
                        //   store i32 1, i32* %2, align 4
                        //   %5 = load i32, i32* %2, align 4
                        //   %6 = add nsw i32 %5, 1
                        //   store i32 %6, i32* %3, align 4
                        //   %7 = load i32, i32* %3, align 4
                        //   %8 = add nsw i32 %7, 1
                        //   store i32 %8, i32* %4, align 4
                        //   ret i32 0
                        // ################
                        //   %1 = alloca i32, align 4
                        //   %2 = alloca i32, align 4
                        //   %3 = alloca i32, align 4
                        //   store i32 0, i32* %1, align 4
                        //   store i32 1, i32* %2, align 4
                        //   %4 = load i32, i32* %2, align 4
                        //   %5 = add nsw i32 %4, 2
                        //   store i32 %5, i32* %3, align 4
                        //   ret i32 0
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
                    MPM.addPass(AddInstrCountPass()),
                    MPM.addPass(InstructionCombiningPass());
                });
        }
    };
}
