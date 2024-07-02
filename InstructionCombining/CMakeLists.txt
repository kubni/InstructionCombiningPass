add_llvm_library( LLVMOurPass MODULE
  OurInstructionCombining.cpp
  IncrementInstructionCombining.cpp

  PLUGIN_TOOL
  opt
)
