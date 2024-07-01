# add_llvm_pass_plugin(LLVMOurPass
#     InstructionCombining.cpp
# )

add_llvm_library( LLVMOurPass MODULE
  InstructionCombining.cpp

  PLUGIN_TOOL
  opt
)
