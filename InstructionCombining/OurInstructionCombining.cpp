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

//3. Compare instructions are converted from <,>,<=,>= to ==,!= if possible
struct ConvertCompareInstructionsPass : public PassInfoMixin<ConvertCompareInstructionsPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        bool changed = false;
        InstructionsToRemove.clear();
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
                            changed = true;
                        }
                    }
                }
            }
            for (Instruction *Instr : InstructionsToRemove) {
                 Instr->eraseFromParent();
            }
            errs() << "New IR: \n" << F << "\n";
        }
        if(changed)
            return PreservedAnalyses::none();
        return PreservedAnalyses::all();
    }
};
//4. All cmp instructions on boolean values are replaced with logical ops
struct ReplaceCompareInstructionsPass : public PassInfoMixin<ReplaceCompareInstructionsPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        bool changed = false;
        InstructionsToRemove.clear();
        for (auto &F : M) {
            mapVariables(F);
            errs() << "Old IR:\n" << F << "\n";
            for (auto &BB : F) {
                for(auto &I : BB) {
                    if (auto *CmpInstr = dyn_cast<ICmpInst>(&I)) {
                        IRBuilder<> Builder(CmpInstr);
                        Value *LHS = CmpInstr->getOperand(0);
                        Value *RHS = CmpInstr->getOperand(1);
                        Value *NewInstr = nullptr;

                        // errs() << "ASds" << *ValuesMap[LHS]->getType() << "\n";
                        // Check if the comparison is on boolean values (i8) in c
                        if (ValuesMap[LHS]->getType()->isIntegerTy(1) && ValuesMap[RHS]->getType()->isIntegerTy(1)) {
                            // errs() << "aaa" << "\n";
                            switch (CmpInstr->getPredicate()) {
                            case ICmpInst::ICMP_EQ:
                                // x == y can be replaced with !(x ^ y )
                                NewInstr =  Builder.CreateNot(Builder.CreateXor(LHS, RHS));
                                break;
                            case ICmpInst::ICMP_NE:
                                // x != y can be replaced with (x ^ y )
                                NewInstr = Builder.CreateXor(LHS, RHS);
                                break;
                            default:
                                break;
                            }
                        }

                        if (NewInstr) {
                            CmpInstr->replaceAllUsesWith(NewInstr);
                            InstructionsToRemove.push_back(CmpInstr);
                            changed = true;
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
        if(changed)
            return PreservedAnalyses::none();
        return PreservedAnalyses::all();
    }
};
//5. add X, X is represented as (X*2) => (X << 1)
struct ReplaceSameOperandsAddWithShlPass : public PassInfoMixin<ReplaceSameOperandsAddWithShlPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        bool changed = false;
        InstructionsToRemove.clear();
        for (auto &F : M) {
            mapVariables(F);
            errs() << "Old IR:\n" << F << "\n";
            for (auto &BB : F) {
                for(auto &I : BB) {
                    if(BinaryOperator* BinaryOp = dyn_cast<BinaryOperator>(&I)) {
                        IRBuilder<> Builder(&I);
                        if(BinaryOp->getOpcode() == Instruction::Add) {
                            if(ValuesMap[BinaryOp->getOperand(0)] == ValuesMap[BinaryOp->getOperand(1)]) {
                                Value *shiftInstr = Builder.CreateShl(BinaryOp->getOperand(0), 1);
                                I.replaceAllUsesWith(shiftInstr);
                                InstructionsToRemove.push_back(&I);
                                changed= true;
                            }

                        }
                    }
                }
            }
            for (Instruction *Instr : InstructionsToRemove) {
                 Instr->eraseFromParent();
            }
            
            errs() << "New IR: \n" << F << "\n";
        }
        if(changed)
            return PreservedAnalyses::none();
        return PreservedAnalyses::all();
    }
};
//6. Multiplies with a power-of-two constant argument are transformed into shifts.
struct ReplacePowerOfTwoMullWithShlPass : public PassInfoMixin<ReplacePowerOfTwoMullWithShlPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        bool changed = false;
        InstructionsToRemove.clear();
        for (auto &F : M) {
            mapVariables(F);
            errs() << "Old IR:\n" << F << "\n";
            for (auto &BB : F) {
                for(auto &I : BB) {
                    if(BinaryOperator* BinaryOp = dyn_cast<BinaryOperator>(&I)) {
                        IRBuilder<> Builder(&I);
                        if(BinaryOp->getOpcode() == Instruction::Mul) {
                            Value *op1 = BinaryOp->getOperand(0);
                            Value *op2 = BinaryOp->getOperand(1);
                            if(ConstantInt *constOp = dyn_cast<ConstantInt>(op2)) {
                                if (constOp->getValue().isPowerOf2()) {
                                    unsigned int shiftAmount = constOp->getValue().logBase2();
                                    Value *shiftInstr = Builder.CreateShl(op1, shiftAmount);
                                    I.replaceAllUsesWith(shiftInstr);
                                    InstructionsToRemove.push_back(&I);
                                    changed = true;
                                }
                            }

                        }

                    }
                }
            }
            for (Instruction *Instr : InstructionsToRemove) {
                 Instr->eraseFromParent();
            }
            
            errs() << "New IR: \n" << F << "\n";
        }
        if(changed)
            return PreservedAnalyses::none();
        return PreservedAnalyses::all();
    }
};

struct InstructionCombiningPass : public PassInfoMixin<InstructionCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        bool changed = false;
        InstructionsToRemove.clear();
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
        
        if(changed)
            return PreservedAnalyses::none();
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
                    MPM.addPass(ConvertCompareInstructionsPass()),
                    MPM.addPass(ReplaceCompareInstructionsPass()),
                    MPM.addPass(ReplaceSameOperandsAddWithShlPass()),
                    MPM.addPass(ReplacePowerOfTwoMullWithShlPass()),
                    MPM.addPass(InstructionCombiningPass());
                });
        }
    };
}
