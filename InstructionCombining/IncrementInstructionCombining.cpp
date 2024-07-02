#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>  // TODO: Unordered map?
using namespace llvm;

namespace {

std::map<std::string, int> AllocaCounts;
std::map<std::string, int> AddCounts;
std::vector<Instruction *> InstructionsToRemove;



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

struct AddCountPass : public PassInfoMixin<AddCountPass> {
        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
            for (auto &F : M) {
                for (auto &BB : F) {
                    for (auto &I : BB) {
                        if (auto *bin_op = dyn_cast<BinaryOperator>(&I)) {
                            if (bin_op->getOpcode() == Instruction::Add)
                                AddCounts[F.getName().str()] += 1;
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
            // errs() << "[InstructionCombiningPass] I saw a function called " << F.getName() << "!\n";
            // LLVMContext &Ctx = F.getContext(); // TODO: Pass this to IR builder and combine it with builder.SetInsertPoint() ?

            errs() << "Old IR: \n" << F << "\n";
            // errs() << "[InstructionCombiningPass] Func <" << F.getName() << ">  ->  " << AllocaCounts.at(F.getName().str()) << " allocas and stores.\n";

            int initial_alloca_count = AllocaCounts.at(F.getName().str());
            int initial_store_count = initial_alloca_count;
            int initial_add_count = AddCounts.at(F.getName().str());
            int tmp_add_count = initial_add_count;

            std::vector<int> initial_store_values(initial_store_count);
            bool isMainStoreSkipped = false;
            bool isFirstLoadSaved = false;
            Value* new_add = nullptr;
            Value* first_loaded_value = nullptr;
            for (auto &BB : F) {
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
                    // Save the store values:
                    if(initial_store_count) {
                        if (auto *store_instruction = dyn_cast<StoreInst>(&I)) {
                            Value* stored_value = store_instruction->getValueOperand();
                            // errs() << "Stored value: " << *stored_value << "\n";
                            initial_store_count--;
                            continue;
                        }
                    }

                    // Save the first load value (that will be the variable that we increment later)
                    if (!isFirstLoadSaved) {
                        if (auto *load_instr = dyn_cast<LoadInst>(&I)) {
                            first_loaded_value = load_instr->getOperand(0);
                            isFirstLoadSaved = true;
                            continue;
                        }

                    }
                    // Now we loop through the instructions until we get to the last add.
                    // We kinda broke the load-store-add pattern here but it doesn't matter.

                    if(tmp_add_count) {
                        if (auto *binary_op = dyn_cast<BinaryOperator>(&I)) {
                            if (binary_op->getOpcode() == Instruction::Add) {
                                // If we are on our last ADD operation, we use its context to create our new improved add which will replace everything later.
                                // TODO: Is is really necessary for this to be here?
                                if (tmp_add_count == 1) {
                                    IRBuilder<> builder(binary_op);
                                    new_add = builder.CreateAdd(first_loaded_value, ConstantInt::get(Type::getInt32Ty(binary_op->getContext()), initial_add_count));

                                    // We replace the final add with our improved add.
                                    binary_op->replaceAllUsesWith(new_add);
                                    // errs() << "New add: " << *new_add << "\n";
                                }
                                tmp_add_count--;
                            }
                        }
                        InstructionsToRemove.push_back(&I);
                        continue;
                    }
                }

                // Finally, we erase all the instructions that are queued for erasure:
                for (Instruction* instr : InstructionsToRemove) {
                    if(instr->isSafeToRemove())
                        instr->eraseFromParent();
                }
            }
            // Print tne new IR
            errs() << "New IR: \n" << F << "\n";


            // TODO: Verify that it is valid IR.
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
                    MPM.addPass(AllocaCountPass());
                    MPM.addPass(AddCountPass());
                    MPM.addPass(IncrementInstructionCombiningPass());
                });
        }
    };
}
