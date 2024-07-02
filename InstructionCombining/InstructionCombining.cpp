#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>  // TODO: Unordered map?
using namespace llvm;

namespace {

std::map<std::string, int> allocaCounts;

struct AllocaCountPass : public PassInfoMixin<AllocaCountPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M) {
            std::string func_name = F.getName().str();
            errs() << "[AllocaCountPass] Currently analyzing " << func_name << "\n";
            for (auto &BB : F) {
                for (auto &I : BB) {
                    Instruction* next_instruction = &I;
                    while(true) {
                        // errs() << "Hello world from AllocaCountPass";
                        auto *alloca_instr = dyn_cast<AllocaInst>(next_instruction);
                        if (alloca_instr == nullptr)
                            // return PreservedAnalyses::all(); // NOTE: Doing this only counts correctly for the first analyzed function
                            break;
                        else {
                            allocaCounts[func_name] += 1;
                            next_instruction = alloca_instr->getNextNonDebugInstruction();
                        }
                    }
                    break; // TODO: Remove this for (auto &I : BB) since we always just want the first one (or more specifically the first sequence of allocas)
                }
                break; // TODO: What if we have multiple basic blocks in a function? Are the allocas still at the beginning? FIXME (Currently we ignore that completely even if they are...)
            }
        }
        return PreservedAnalyses::all();
    };
};

struct InstructionCombiningPass : public PassInfoMixin<InstructionCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {

        for (auto &F : M) {
            errs() << "I saw a function called " << F.getName() << "!\n";
            // LLVMContext &Ctx = F.getContext(); // TODO: Pass this to IR builder and combine it with builder.SetInsertPoint() ?

            errs() << "Old IR: \n" << F << "\n";

             /*   TODO: Pattern is actually load-add-store
             *         Before that, we have alloca and store for each function argument. This needs to be separate
             *        
             *
             */

            errs() << "Func <" << F.getName() << ">  ->  " << allocaCounts.at(F.getName().str()) << "\n";



            for (auto &BB : F) {
                for (auto &I : BB) {

                    // Check if we have an `alloca` instruction:
                    if (auto *alloca_instruction = dyn_cast<AllocaInst>(&I)) {
                        // Check if its for an integer:
                        // TODO: This will break the optimization if one of the function arguments is non-int...
                        if(alloca_instruction->getAllocatedType()->isIntegerTy()) {
                            // Get the next instruction
                            Instruction* next_instruction = alloca_instruction->getNextNonDebugInstruction();
                            // Check if its a `store` instruction
                            if (auto *store_instruction = dyn_cast<StoreInst>(next_instruction)) {
                                // Grab the stored value
                                Value* stored_value = store_instruction->getValueOperand(); // TODO: Unneeded?
                                errs() << "Stored value: " << *stored_value << "\n";

                                // .... In the next steps we do similar things to above
                                next_instruction = store_instruction->getNextNonDebugInstruction();
                                if (auto *load_instruction = dyn_cast<LoadInst>(next_instruction)) {
                                    Value* loaded_value = load_instruction->getOperand(0);
                                    errs() << "Loaded value: " << *loaded_value << "\n";
                                   
                                    next_instruction = load_instruction->getNextNonDebugInstruction();
                                    if(auto *op = dyn_cast<BinaryOperator>(next_instruction)) {
                                        if (op->getOpcode() == Instruction::Add /*TODO: && op->hasOneUse()*/) {
                                            // NOTE: Does nothing currently except print the IR again
                                            errs() << "New IR: " << F << "\n";
                                            // return PreservedAnalyses::none();
                                        }
                                    }
                                }

                            }
                        }


                    }
                }
            }
        }
        return PreservedAnalyses::all();  // TODO: Change this if we go back to the old pass manager
    };
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
                    MPM.addPass(InstructionCombiningPass());
                });
        }
    };
}
