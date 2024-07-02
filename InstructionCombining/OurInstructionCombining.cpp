#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>  // TODO: Unordered map?
#include <unordered_map>
#include <llvm/IR/IRBuilder.h>
using namespace llvm;

namespace {
std::vector<Instruction *> InstructionsToRemove;
std::unordered_map<Value*, Value *> ValuesMap;
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

void mapVariables(Function &F) {
    for (auto &BB : F) {
        for(auto &I : BB) {
            if(isa<LoadInst>(&I)) {
                ValuesMap[&I] = I.getOperand(0);
            }
            if(isa<StoreInst>(&I)) {
                ValuesMap[I.getOperand(0)] = I.getOperand(1);
            }
        }
    }
}

struct InstructionCombiningPass : public PassInfoMixin<InstructionCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        bool changed = false;
        for (auto &F : M) {
            mapVariables(F);
            errs() << "Old IR: \n" << F << "\n";
            for (auto &BB : F) {
                for(auto &I : BB) {
                    // if(LoadInst *LoadInstr  = dyn_cast<Loa
                    errs() << "sss" << "\n";
                    //     ValuesMap[LoadInstr] = LoadInstr->getOperand(0);
                    // }
                    if (BinaryOperator *BinaryOp = dyn_cast<BinaryOperator>(&I)) {
                        if(isa<AddOperator>(BinaryOp)) {
                            if (auto *Const1 = dyn_cast<ConstantInt>(BinaryOp->getOperand(1))) {
                                //needed to look for other add instructions
                                for(auto nextIt = std::next(BasicBlock::iterator(BinaryOp)); nextIt != BB.end(); ++nextIt) {
                                    errs() << "aaa" << "\n";
                                    if(BinaryOperator *BinaryOp2 = dyn_cast<BinaryOperator>(&*nextIt)) {
                                        if(isa<AddOperator>(BinaryOp2) && ValuesMap[BinaryOp2->getOperand(0)] == ValuesMap[BinaryOp]) {
                                            IRBuilder<> Builder(BinaryOp2);
                                            Instruction *NewAdd = (Instruction*) Builder.CreateAdd(BinaryOp2->getOperand(0),ConstantInt::get(Const1->getType(), 2));
                                            NewAdd->insertAfter(BinaryOp2);
                                            BinaryOp2->replaceAllUsesWith(NewAdd);
                                            InstructionsToRemove.push_back(BinaryOp);
                                            InstructionsToRemove.push_back(BinaryOp2);                                            
                                            // changed = true;
                                            // it = nextIt; // Move iterator to next valid position
                                            break; // Break out of inner loop to continue with outer loop
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if(changed)
                        break;
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

        for (Instruction *Instr : InstructionsToRemove) {
            Instr->eraseFromParent();
        }
        
        for (auto &F : M) {
            errs() << "new IR: \n" << F << "\n";
        }
        errs() << "sadas" << "\n";
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
                    // MPM.addPass(AddInstrCountPass()),
                    MPM.addPass(InstructionCombiningPass());
                });
        }
    };
}
