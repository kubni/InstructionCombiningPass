#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/IRBuilder.h"

#include <unordered_map>
using namespace llvm;

namespace {

std::unordered_map<std::string, int> AllocaCounts;
std::unordered_map<Value*, int> PatternCounts;
std::unordered_map<Value*, int> InitialStoredValues;
std::vector<Instruction *> InstructionsToRemove;


std::unordered_map<Value*, Value *> ValuesMap;
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

// This pass moves constants to RHS in a binary operation
struct RHSMovePass : public PassInfoMixin<RHSMovePass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            errs() << "Old IR: " << F << "\n";
            for (auto &BB : F) {
                for (auto &I : BB) {
                    if(auto* binary_op = dyn_cast<BinaryOperator>(&I)) {
                        // We only do a move to RHS if we have Mul or Add
                        auto opcode = binary_op->getOpcode();
                        Value* op1 = binary_op->getOperand(0);
                        Value* op2 = binary_op->getOperand(1);
                        switch(opcode) {
                            case Instruction::Add:
                                if (isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
                                    IRBuilder<> builder(binary_op);
                                    Value* new_add = builder.CreateAdd(op2, op1);
                                    binary_op->replaceAllUsesWith(new_add);
                                    InstructionsToRemove.push_back(binary_op);
                                }
                                break;
                            case Instruction::Mul:
                                if (isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
                                    IRBuilder<> builder(binary_op);
                                    Value* new_mul = builder.CreateMul(op2, op1);
                                    binary_op->replaceAllUsesWith(new_mul);
                                    InstructionsToRemove.push_back(binary_op);
                                }
                                break;

                            default:
                                continue;
                        }
                    }
                }
            }
            for (Instruction* i : InstructionsToRemove) {
                i->eraseFromParent();
            }
           // We also need to remove the instruction from the vector, in order for it to be clean for next passes to use.
           InstructionsToRemove.clear();
           // InstructionsToRemove.shrink_to_fit();
           errs() << "New IR: " << F << "\n";
        }
        return PreservedAnalyses::all();
    }
};

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


    

struct AllocaCountPass : public PassInfoMixin<AllocaCountPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            std::string func_name = F.getName().str();
            // errs() << "[AllocaCountPass] Currently analyzing " << func_name << "\n";
            for (auto &BB : F) {
                Instruction* next_instruction = &BB.front();
                while(true) {
                    auto *alloca_instr = dyn_cast<AllocaInst>(next_instruction);
                    if (alloca_instr == nullptr)
                        break;
                    else {
                        AllocaCounts[func_name] += 1;
                        next_instruction = alloca_instr->getNextNonDebugInstruction();
                    }
                }
                break;
            }
        }
        return PreservedAnalyses::all();
    }
};



struct PatternCountPass : public PassInfoMixin<PatternCountPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            std::string function_name = F.getName().str();
            for (auto &BB : F) {
                int alloca_count = AllocaCounts[F.getName().str()];
                int tmp_alloca_store_count = alloca_count * 2;
                for (auto &I : BB)
                {
                    // Skip the alloca and store instructions:
                    if (tmp_alloca_store_count) {
                        tmp_alloca_store_count--;
                        continue;
                    }

                    if (auto *load_instruction = dyn_cast<LoadInst>(&I)) {
                        Value* load_ptr = load_instruction->getPointerOperand(); // NOTE: This should be equal to load_instruction->getOperand(0);

                        Instruction* next_instruction = I.getNextNonDebugInstruction();
                        if (auto *binary_op = dyn_cast<BinaryOperator>(next_instruction)) {
                            if(binary_op->getOpcode() == Instruction::Add) {
                                // Save the second add operand, so that we can support combining for += 2 or += <something> instead of just ++.
                                Value* right_operand = binary_op->getOperand(1);
                                next_instruction = next_instruction->getNextNonDebugInstruction();
                                if (auto *store_instruction = dyn_cast<StoreInst>(next_instruction)) {

                                    Value* stored_value = store_instruction->getOperand(0);
                                    Value* ptr_to_storage = store_instruction->getOperand(1);

                                    if (stored_value == binary_op  && ptr_to_storage == load_ptr) {

                                        // Instead of just increasing this by 1 (which corresponds to an increment), we will increase it by the add's right operand, so we can cover += 2, += 3, etc.
                                        if(ConstantInt* ci_right_op = dyn_cast<ConstantInt>(right_operand)) {
                                            PatternCounts[load_ptr] += ci_right_op->getSExtValue();
                                        }

                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return PreservedAnalyses::all();
    }
};

struct IncrementInstructionCombiningPass : public PassInfoMixin<IncrementInstructionCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {

        for (auto &F : M) {
            errs() << "Old IR: \n" << F << "\n";

            int initial_alloca_count = AllocaCounts.at(F.getName().str());
            int initial_store_count = initial_alloca_count;

            std::vector<int> initial_store_values(initial_store_count);
            bool isMainStoreSkipped = false;
            bool isFirstLoadSaved = false;
            Value* new_add = nullptr;
            Value* first_loaded_value = nullptr;

            for (auto &BB : F) {
                StoreInst* instruction_to_skip = nullptr;
                for (auto &I : BB) {
                    // Skip initial allocas:
                    if (initial_alloca_count) {
                        initial_alloca_count--;
                        continue;
                    }

                    // NOTE: Specially for main(), there is an additional store (always the first store coming after allocas) that we want to skip
                    if (F.getName().str() == "main" && !isMainStoreSkipped) {
                        isMainStoreSkipped = true;
                        initial_store_count--;
                        continue;
                    }

                    if(initial_store_count > 0) {
                        if (auto *store_instruction = dyn_cast<StoreInst>(&I)) {
                            // We need a way to skip our custom stores that we added in here, so that they don't trigger the if when I becomes that new store instruction.
                            if (store_instruction != instruction_to_skip) {

                                Value* stored_value = store_instruction->getValueOperand();
                                Value* ptr_to_storage = store_instruction->getPointerOperand();

                                // Create the load instruction that loads the stored value and then our custom add instruction for that variable.
                                // After that, store that at the original location.
                                IRBuilder<> builder(store_instruction);
                                builder.SetInsertPoint(&BB, ++builder.GetInsertPoint());
                                LoadInst* new_load_inst = builder.CreateLoad(stored_value->getType(), ptr_to_storage);
                                Value* new_add_inst = builder.CreateAdd(new_load_inst, ConstantInt::get(stored_value->getType(), PatternCounts.at(new_load_inst->getPointerOperand())));


                                // We can't just simply add the store instruction like we added load and add, because when I becomes this new store instruction, it will enter the if, and we don't want that for out custom store instructions.
                                // We need a way to skip our custom stores
                                StoreInst* new_store_inst = builder.CreateStore(new_add_inst, ptr_to_storage);
                                instruction_to_skip = new_store_inst;

                                initial_store_count--;
                            }
                        }
                        continue;
                    }

                    if(initial_store_count > -3) {
                        initial_store_count--;
                        // If we encounter a return instruction here, we stop in order to prevent a segfault:
                        if (auto* ret_inst = dyn_cast<ReturnInst>(&I)) {
                            errs() << "New IR: \n" << F << "\n";
                            return PreservedAnalyses::none();
                        } else
                            continue;
                    }

                   

                    // We just want to remove load-store-adds that are leftover from original code, not all instructions that come after our newly added ones.
                    if (auto* pattern_load = dyn_cast<LoadInst>(&I)) {
                        // errs() << "Pattern load to remove: " << *pattern_load << "\n";
                        Instruction* next_instruction = pattern_load->getNextNonDebugInstruction();
                        if (auto* bin_op = dyn_cast<BinaryOperator>(next_instruction)) {
                            if (bin_op->getOpcode() == Instruction::Add) {
                                // errs() << "Pattern add to remove: " << *bin_op << "\n";
                                next_instruction = bin_op->getNextNonDebugInstruction();
                                if (auto* pattern_store = dyn_cast<StoreInst>(next_instruction)) {
                                    // errs() << "Pattern store to remove: " << *pattern_store << "\n";
                                    InstructionsToRemove.push_back(pattern_store);
                                    InstructionsToRemove.push_back(bin_op);
                                    InstructionsToRemove.push_back(pattern_load);
                                }
                            }
                        }
                    } 
                }


                // Finally, we erase all the instructions that are queued for erasure:
                for (Instruction* instr : InstructionsToRemove) {
                    instr->eraseFromParent();
                }
            }
            // Print tne new IR
            errs() << "New IR: \n" << F << "\n";

            // Verify that it is valid IR.
            if (verifyFunction(F, &errs()))
                errs() << "Function " << F.getName() << " is invalid!\n";
            else
                errs() << "Function " << F.getName() << " is valid!\n";

        }

        InstructionsToRemove.clear();
        return PreservedAnalyses::none();
    }
};


// int x = 0;   x += 4;   -------> int x = 4;
/*
** NOTE: Idea:
** For each initial allocated variable we know that there is only one add.
** We want to use that RHS add operand and change the initial store for that specific variable.
** We already have PatternCount (which doesn't really count them anymore) that has initial allocated variables mapped to that RHS value
** That way, we can easily change the store instruction.
** The problem is how to delete now unnecessary load-add-store that corresponds to that specific variable
**    NOTE: Idea:
**                We search for the code and just check if its load-add-store sequence that uses the pointer to that initial allocated value
**
**
 */
struct InitStoreCombiningPass : public PassInfoMixin<InitStoreCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {

        for (auto &F : M) {

            errs() << "Old IR: " << F << "\n";
            int initial_alloca_count = AllocaCounts.at(F.getName().str());
            bool isMainStoreSkipped = false;
            bool isStoreInstToSkip = false;

            for (auto &BB : F) {
                for (auto &I : BB) {
                    // Skip the initial alloca instructions and that specific main store instruction if we are in main:
                    if (initial_alloca_count) {
                        initial_alloca_count--;
                        continue;
                    }

                    if (F.getName().str() == "main" && !isMainStoreSkipped) {
                        isMainStoreSkipped = true;
                        // initial_store_count--;
                        continue;
                    }


                    // First, we see at what store inst are we now:
                    if(auto* current_store_inst = dyn_cast<StoreInst>(&I)) {
                        // We don't want to do this if we are currently at the previously newly created store inst:
                        if(!isStoreInstToSkip) {

                            // We schedule this initial store for deletion:
                            InstructionsToRemove.push_back(current_store_inst);

                            // We need to store the initial stored value, because of the following case:
                            /**
                             *    int x = 5;
                             *    x += 3;
                             *
                             *    This should be int x = 8;
                             *    If we don't remember the initial stored value, we would just overwrite it with 3.
                             */
                            Value* stored_value = current_store_inst->getValueOperand();
                            Value* ptr_to_storage = current_store_inst->getPointerOperand();
                            if (ConstantInt* ci_stored_value = dyn_cast<ConstantInt>(stored_value)) {
                                InitialStoredValues[ptr_to_storage] = ci_stored_value->getSExtValue();
                            }

                            IRBuilder<> builder (current_store_inst);
                            builder.SetInsertPoint(&BB, ++builder.GetInsertPoint());

                            Value* ci_value = ConstantInt::get(current_store_inst->getValueOperand()->getType(), InitialStoredValues.at(ptr_to_storage) + PatternCounts.at(ptr_to_storage));
                            StoreInst* new_store_inst = builder.CreateStore(ci_value, ptr_to_storage);
                            isStoreInstToSkip = true;

                            continue;
                        } else {
                            // Reset;
                            isStoreInstToSkip = false;
                        }
                    } else if (auto* current_load_inst = dyn_cast<LoadInst>(&I)) {
                       // This means that we passed all store instructions
                       // Now we check for the pattern and delete those load-add-store instructions,
                       // but ONLY if they correspond to x++; or x += <something>.
                       Value* load_ptr = current_load_inst->getPointerOperand();
                       Instruction* next_instruction = I.getNextNonDebugInstruction();
                       if (auto *binary_op = dyn_cast<BinaryOperator>(next_instruction)) {
                            if(binary_op->getOpcode() == Instruction::Add) {
                                next_instruction = next_instruction->getNextNonDebugInstruction();

                                if (auto *store_instruction = dyn_cast<StoreInst>(next_instruction)) {

                                    // Important: We want to skip this one, like the ones we created,
                                    // in order not to trigger the if above (which should only trigger for initial stores)
                                    isStoreInstToSkip = true;

                                    Value* stored_value = store_instruction->getOperand(0);
                                    Value* ptr_to_storage = store_instruction->getOperand(1);

                                    if (stored_value == binary_op  && ptr_to_storage == load_ptr) {
                                        InstructionsToRemove.push_back(store_instruction);
                                        InstructionsToRemove.push_back(binary_op);
                                        InstructionsToRemove.push_back(current_load_inst);
                                    }
                                }
                            }
                       }
                    }
                }

                for (Instruction* i :  InstructionsToRemove) {
                    i->eraseFromParent();
                }
            }

            errs() << "New IR: " << F << "\n";
            // Verify that it is valid IR.
            if (verifyFunction(F, &errs()))
                errs() << "Function " << F.getName() << " is invalid!\n";
            else
                errs() << "Function " << F.getName() << " is valid!\n";


        }
        InstructionsToRemove.clear();
        return PreservedAnalyses::none();
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
                    MPM.addPass(RHSMovePass());
                    MPM.addPass(ConvertCompareInstructionsPass());
                    MPM.addPass(ReplaceCompareInstructionsPass());
                    MPM.addPass(ReplaceSameOperandsAddWithShlPass());
                    MPM.addPass(ReplacePowerOfTwoMullWithShlPass());
                    MPM.addPass(AllocaCountPass());
                    MPM.addPass(PatternCountPass());
                    MPM.addPass(IncrementInstructionCombiningPass());
                    MPM.addPass(InitStoreCombiningPass());
                });
        }
    };
}
