add_llvm_library( LLVMOurPass MODULE
  IncrementInstructionCombining.cpp
  OurInstructionCombining.cpp

  PLUGIN_TOOL
  opt
)
