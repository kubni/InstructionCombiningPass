add_llvm_library( LLVMOurPass MODULE
  IncrementInstructionCombining.cpp

  PLUGIN_TOOL
  opt
)
