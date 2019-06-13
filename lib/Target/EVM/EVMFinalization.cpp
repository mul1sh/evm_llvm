//===-- EVMFinalization.cpp - Argument instruction moving ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-expand-pseudos"

namespace {
class EVMFinalization final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMFinalization() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM add Jumpdest";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const TargetInstrInfo* TII;
  const EVMSubtarget* ST;

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool shouldInsertJUMPDEST(MachineBasicBlock &MBB) const;
};
} // end anonymous namespace

char EVMFinalization::ID = 0;
INITIALIZE_PASS(EVMFinalization, DEBUG_TYPE,
                "Finalize the codegen",
                false, false)

FunctionPass *llvm::createEVMFinalization() {
  return new EVMFinalization();
}

bool EVMFinalization::shouldInsertJUMPDEST(MachineBasicBlock &MBB) const {

  // Entry MBB needs a basic block.
  if (&MBB == &MBB.getParent()->front()) {
    return true;
  }

  // for now we will add a JUMPDEST anyway.
  return true;
}

bool EVMFinalization::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Expand pseudo instructions **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  this->ST = &MF.getSubtarget<EVMSubtarget>();
  this->TII = ST->getInstrInfo();

  bool Changed = false;

  for (MachineBasicBlock & MBB : MF) {
    // Insert JUMPDEST at the beginning of the MBB is necessary

    if (shouldInsertJUMPDEST(MBB)) {
      MachineBasicBlock::iterator begin = MBB.begin();
      BuildMI(MBB, begin, begin->getDebugLoc(), TII->get(EVM::JUMPDEST));
    }

    // use iterator since we need to remove pseudo instructions
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end();
         I != E;) {

      MachineInstr *MI = &*I++; 
      unsigned opcode = MI->getOpcode();

      // Remove pseudo instruction
      if (opcode == EVM::pSTACKARG) {
        MI->eraseFromParent();
        Changed = true;
      }
      
      if (opcode == EVM::pRETURNSUB) {
        // expand it to
        // SWAP1
        // JUMP
        BuildMI(MBB, MI, MI->getDebugLoc(), TII->get(EVM::SWAP1));
        BuildMI(MBB, MI, MI->getDebugLoc(), TII->get(EVM::JUMP));
        MI->eraseFromParent();
        Changed = true;
      }

      if (opcode == EVM::pJUMPSUB) {
        // simply convert it to JUMP
        MI->setDesc(TII->get(EVM::JUMP));
        Changed = true;
      }

    }
  }

  return Changed;
}