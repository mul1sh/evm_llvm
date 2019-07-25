//===- EVMDisassembler.cpp - Disassembler for EVM ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the EVM Disassembler.
//
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMRegisterInfo.h"
#include "EVMSubtarget.h"

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "TargetInfo/EVMTargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "evm-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for EVM.
class EVMDisassembler final : public MCDisassembler {
public:
  EVMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  virtual ~EVMDisassembler() = default;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &VStream,
                              raw_ostream &CStream) const override;
};
}

static MCDisassembler *createEVMDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  return new EVMDisassembler(STI, Ctx);
}


extern "C" void LLVMInitializeEVMDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheEVMTarget(),
                                         createEVMDisassembler);
}

#include "EVMGenDisassemblerTables.inc"

DecodeStatus EVMDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address,
                                             raw_ostream &VStream,
                                             raw_ostream &CStream) const {
  DecodeStatus Result;

  return Result;
}

