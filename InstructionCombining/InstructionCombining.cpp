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
                Instruction* next_instruction = &BB.front();
                while(true) {
                    auto *alloca_instr = dyn_cast<AllocaInst>(next_instruction);
                    if (alloca_instr == nullptr)
                        break;
                    else {
                        allocaCounts[func_name] += 1;
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

struct InstructionCombiningPass : public PassInfoMixin<InstructionCombiningPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {

        for (auto &F : M) {
            errs() << "[InstructionCombiningPass] I saw a function called " << F.getName() << "!\n";
            // LLVMContext &Ctx = F.getContext(); // TODO: Pass this to IR builder and combine it with builder.SetInsertPoint() ?

            errs() << "Old IR: \n" << F << "\n";
            errs() << "[InstructionCombiningPass] Func <" << F.getName() << ">  ->  " << allocaCounts.at(F.getName().str()) << " allocas and stores.\n";
            int alloca_count = allocaCounts.at(F.getName().str());

            for (auto &BB : F) {

                // errs() << "BB: " << BB;

                Instruction* next_instruction = &BB.front();
                for (auto &I : BB) {
                // while(BasicBlock::iterator(next_instruction) != BB.end()) {

                    // Skip initial allocas and stores:
                    // if(alloca_count && dyn_cast<StoreInst>(&I)) {
                    //     alloca_count--;
                    //     continue;
                    // }



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
