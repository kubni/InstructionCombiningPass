#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Verifier.h"

#include <map>  // TODO: Unordered map?
using namespace llvm;

namespace {

std::map<std::string, int> AllocaCounts;
std::map<Value*, int> PatternCounts;
std::vector<Instruction *> InstructionsToRemove;



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
                                    // builder.SetInsertPoint(&BB, ++builder.GetInsertPoint());
                                    Value* new_add = builder.CreateAdd(op2, op1);
                                    binary_op->replaceAllUsesWith(new_add);
                                    InstructionsToRemove.push_back(binary_op);
                                }
                                break;
                            case Instruction::Mul:
                                if (isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
                                    IRBuilder<> builder(binary_op);
                                    // builder.SetInsertPoint(&BB, ++builder.GetInsertPoint());
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
        return PreservedAnalyses::none();
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
                break; // TODO: What if we have multiple basic blocks in a function? Are the allocas still at the beginning? FIXME (Currently we ignore that completely even if they are...)
                                // NOTE: The allocas still happen at the beginning of the function, though it seems that the store instruction happens in the branch, which makes sense, but breaks our algorithm FIXME.
            }
        }
        return PreservedAnalyses::all();
    }
};

// TODO: Check if basic example works in a separate function (that isn't main())

/*
 *           2) TODO: There are probably other combinations of code that produce load-add-store that aren't increments
 *
 *           3) NOTE: We could use this to actually allow more general combinations of instructions, for example: x+=2; x+=2; ----> x+=4;
 *
 * */


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

                                next_instruction = next_instruction->getNextNonDebugInstruction();
                                if (auto *store_instruction = dyn_cast<StoreInst>(next_instruction)) {

                                    /*
                                    **  NOTE: Idea:
                                    **  How do we know which variable's value has increased?
                                    **  We check the following:
                                    **      Stored value has to be the same as add result, and it has to be stored at same place where we allocated memory first.
                                    **      How do we check that?
                                    **           That pointer has to be the same as the load instruction's operand
                                     */

                                    Value* stored_value = store_instruction->getOperand(0);
                                    Value* ptr_to_storage = store_instruction->getOperand(1);



                                    if (stored_value == binary_op  && ptr_to_storage == load_ptr) {
                                        // If this is true, we will remember that variable at the original allocated memory space was increased by 1.
                                        // But we need to somehow get that variable.


                                        // That variable is on the left side of the current load_instr!
                                        // However, for llvm these are different temporary variables, even though they all represent the same var (x for example), and we want map that has [x, 2] for example.
                                        // This is problematic
                                             // We can store ptr %2 (load_instructions pointer operand) as a key! That way we won't save different states of x as different temporary variables,
                                             // instead we will always know that its that one.

                                        PatternCounts[load_ptr] += 1;
                                    }
                                }
                            }
                        } else {
                            continue; //TODO: This will do the same thing as the continue in load's else block, since we haven't moved I to be the next_instruction?
                        }
                    } else {
                        continue;
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
                                // TODO: Our current code inserts load and add directly after store.
                                //       Original llvm code first does the stores and then does loads and adds it seems? Is this even important?
                                IRBuilder<> builder(store_instruction);
                                builder.SetInsertPoint(&BB, ++builder.GetInsertPoint());
                                LoadInst* new_load_inst = builder.CreateLoad(stored_value->getType(), ptr_to_storage);
                                Value* new_add_inst = builder.CreateAdd(new_load_inst, ConstantInt::get(stored_value->getType(), PatternCounts.at(new_load_inst->getPointerOperand())));


                                // We can't just simply add the store instruction like we added load and add, because when I becomes this new store instruction, it will enter the if, and we don't want that for out custom store instructions.
                                // We need a way to skip our custom stores
                                StoreInst* new_store_inst = builder.CreateStore(new_add_inst, ptr_to_storage);
                                instruction_to_skip = new_store_inst;
                                // errs() << "Problematic instruction: " << *instruction_to_skip << "\n";

                                // We add the final load so that return can return it.
                                // LoadInst* final_load_inst = builder.CreateLoad(new_store_inst->getValueOperand()->getType(), ptr_to_storage);

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
                            return PreservedAnalyses::none(); // TODO: all() ?
                        } else
                            continue;
                    }

                   

                    // We just want to remove load-store-adds that are leftover from original code, not all instructions that come after our newly added ones.
                    if (auto* pattern_load = dyn_cast<LoadInst>(&I)) {
                        // errs() << "Pattern load to remove: " << *pattern_load << "\n";
                        Instruction* next_instruction = pattern_load->getNextNonDebugInstruction();
                        if (auto* bin_op = dyn_cast<BinaryOperator>(next_instruction)) {
                            if (bin_op->getOpcode() == Instruction::Add && ConstantInt::get(bin_op->getType(), 1) == bin_op->getOperand(1)) {
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
                    // MPM.addPass(AllocaCountPass());
                    // MPM.addPass(PatternCountPass());
                    // MPM.addPass(IncrementInstructionCombiningPass());
                    MPM.addPass(RHSMovePass());
                });
        }
    };
}
