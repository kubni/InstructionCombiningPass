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
            if(isa<ZExtInst>(&I)) {
                ValuesMap[&I] = I.getOperand(0);
            }
        }
    }
}

//3.
struct ConvertCompareInstructionsPass : public PassInfoMixin<ConvertCompareInstructionsPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            errs() << "Old IR: \n" << F << "\n";
            for (auto &BB : F) {
                for(auto &I : BB) {
                    if (auto *CmpInstr = dyn_cast<ICmpInst>(&I)) {
                        IRBuilder<> Builder(CmpInstr);
                        Value *LHS = CmpInstr->getOperand(0);
                        Value *RHS = CmpInstr->getOperand(1);
                        Value *NewInstr = nullptr;

                        if (auto *RHSConstant = dyn_cast<ConstantInt>(RHS)) {
                            if (RHSConstant->isZero()) {
                                switch (CmpInstr->getPredicate()) {
                                    case ICmpInst::ICMP_SLT:
                                    case ICmpInst::ICMP_ULT:
                                        // x < 0 is false for unsigned and x < 0 can be x == 0 for signed
                                        NewInstr = (Value*) Builder.getFalse();
                                        break;
                                    case ICmpInst::ICMP_SGT:
                                    case ICmpInst::ICMP_UGT:
                                        // x > 0 can be simplified to x != 0
                                        NewInstr = Builder.CreateICmpNE(LHS, RHS);
                                        break;
                                    case ICmpInst::ICMP_SLE:
                                    case ICmpInst::ICMP_ULE:
                                    // x <= 0 can be simplified to x == 0
                                        NewInstr = Builder.CreateICmpEQ(LHS, RHS);
                                    break;
                                    case ICmpInst::ICMP_SGE:
                                    case ICmpInst::ICMP_UGE:
                                        // x >= 0 is true for unsigned and x >= 0 can be simplified to true for signed
                                        NewInstr = (Value*) Builder.getTrue();
                                        break;
                                    default:
                                        break;
                                }
                            }
                        }

                        if (NewInstr) {
                            CmpInstr->replaceAllUsesWith(NewInstr);
                            InstructionsToRemove.push_back(CmpInstr);
                        }
                    }
                }
            }
            for (Instruction *Instr : InstructionsToRemove) {
                 Instr->eraseFromParent();
            }
            errs() << "New IR: \n" << F << "\n";
        }
        return PreservedAnalyses::all();
    }
};

struct ReplaceCompareInstructionsPass : public PassInfoMixin<ReplaceCompareInstructionsPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            mapVariables(F);
            errs() << "Old IR:aaa \n" << F << "\n";
            for (auto &BB : F) {
                for(auto &I : BB) {
                    if (auto *CmpInstr = dyn_cast<ICmpInst>(&I)) {
                        IRBuilder<> Builder(CmpInstr);
                        Value *LHS = CmpInstr->getOperand(0);
                        Value *RHS = CmpInstr->getOperand(1);
                        Value *NewInstr = nullptr;

                        errs() << "ASds" << *ValuesMap[LHS]->getType() << "\n";
                        // Check if the comparison is on boolean values (i8) in c
                        if (ValuesMap[LHS]->getType()->isIntegerTy(1) && ValuesMap[RHS]->getType()->isIntegerTy(1)) {
                            errs() << "aaa" << "\n";
                            switch (CmpInstr->getPredicate()) {
                            case ICmpInst::ICMP_EQ:
                                // x == y can be replaced with x && y 
                                NewInstr = Builder.CreateAnd(LHS, RHS);
                                break;
                            case ICmpInst::ICMP_NE:
                                // x != y can be replaced with x || y
                                NewInstr = Builder.CreateOr(LHS, RHS);
                                break;
                            default:
                                break;
                            }
                        }

                        if (NewInstr) {
                            CmpInstr->replaceAllUsesWith(NewInstr);
                            InstructionsToRemove.push_back(CmpInstr);
                            // CmpInstr->eraseFromParent();

                        }
                    
                     }
                }
            }
            for (Instruction *Instr : InstructionsToRemove) {
                 Instr->eraseFromParent();
            }
            errs() << "New IR: \n" << F << "\n";
        }
        return PreservedAnalyses::all();
    }
};

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
                    // MPM.addPass(ConvertCompareInstructionsPass()),
                    MPM.addPass(ReplaceCompareInstructionsPass());
                    // MPM.addPass(AddInstrCountPass()),
                    // MPM.addPass(InstructionCombiningPass());
                });
        }
    };
}
