#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Verifier.h"
#include <llvm/IR/IRBuilder.h>

#include <map>  // TODO: Unordered map?
#include <unordered_map>
using namespace llvm;

namespace {
std::vector<Instruction *> InstructionsToRemove;
std::unordered_map<Value*, Value *> ValuesMap;
AllocaInst* returnValue;
int addInstrCount = 0;


struct AddInstrCountPass : public PassInfoMixin<AddInstrCountPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            for (auto &BB : F) {
                for(auto &I : BB) {
                    if(isa<AllocaInst>(&I)) {
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
                    if (BinaryOperator *BinaryOp = dyn_cast<BinaryOperator>(&I)) {
                        if(isa<AddOperator>(BinaryOp)) {
                            if (auto *Const1 = dyn_cast<ConstantInt>(BinaryOp->getOperand(1))) {
                                //needed to look for other add instructions that is after one we are one
                                for(auto nextIt = std::next(BasicBlock::iterator(BinaryOp)); nextIt != BB.end(); ++nextIt) {

                                    //this is needed to clear all load and store instructions that use variable we dont want
                                  if (LoadInst *LoadInstr = dyn_cast<LoadInst>(&*nextIt)) {
                                        if (LoadInstr->getOperand(0) == ValuesMap[BinaryOp]) {
                                            InstructionsToRemove.push_back(LoadInstr);
                                        }
                                    } else if (StoreInst *StoreInstr = dyn_cast<StoreInst>(&*nextIt)) {
                                        if (StoreInstr->getOperand(1) == ValuesMap[BinaryOp]) {
                                            InstructionsToRemove.push_back(StoreInstr);
                                        }
                                    }
                                    if(BinaryOperator *BinaryOp2 = dyn_cast<BinaryOperator>(&*nextIt)) {
                                        if(isa<AddOperator>(BinaryOp2) && ValuesMap[BinaryOp2->getOperand(0)] == ValuesMap[BinaryOp]) {
                                            IRBuilder<> Builder(BinaryOp2);
                                            Builder.SetInsertPoint(&BB, ++Builder.GetInsertPoint());
                                            //create new add that combines 2 instructions
                                            Instruction *NewAdd = (Instruction*) Builder.CreateAdd(BinaryOp->getOperand(0),ConstantInt::get(Const1->getType(), 2));
                                            BinaryOp2->replaceAllUsesWith(NewAdd);
                                            // NewStore->insertAfter(NewAdd);
                                            InstructionsToRemove.push_back(BinaryOp);
                                            //Needed to remove aloc of variable we dont need
                                            InstructionsToRemove.push_back((Instruction*) ValuesMap[BinaryOp]);
                                            InstructionsToRemove.push_back(BinaryOp2);                                            
                                            changed = true;
                                            
                                            break; // Break out of inner loop to continue with outer loop
                                        }
                                    }
                                }
                            }
                        }
                    }
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
                        for (Instruction *Instr : InstructionsToRemove) {
                             Instr->eraseFromParent();
                         }
                }
                errs() << "New IR: \n" << F << "\n";
                 // Verify that it is valid IR.
                if (verifyFunction(F, &errs()))
                    errs() << "Function " << F.getName() << " is invalid!\n";
                else
                    errs() << "Function " << F.getName() << " is valid!\n";

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
