//===- EVMStackAllocAnalysis ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the AMGPU address space based alias analysis pass.
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMStackAllocAnalysis.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "evm-stackalloc"

// Register this pass...
char EVMStackAlloc::ID = 0;

INITIALIZE_PASS(EVMStackAlloc, "evm-stackalloc",
                "Stack Allocation Analysis", false, true)


unsigned EVMStackStatus::getStackDepth() const {
  return stackElements.size();
}

unsigned EVMStackStatus::get(unsigned depth) const {
  return stackElements.rbegin()[depth];
}

void EVMStackStatus::swap(unsigned depth) {
    if (depth == 0) {
      return;
    }
    assert(stackElements.size() >= 2);
    LLVM_DEBUG({
      unsigned first = stackElements.rbegin()[0];
      unsigned second = stackElements.rbegin()[depth];
      unsigned fst_idx = Register::virtReg2Index(first);
      unsigned snd_idx = Register::virtReg2Index(second);
      dbgs() << "    SWAP" << depth << ": Swapping %" << fst_idx << " and %"
             << snd_idx << "\n";
    });
    std::iter_swap(stackElements.rbegin(), stackElements.rbegin() + depth);
}

void EVMStackStatus::dup(unsigned depth) {
  unsigned elem = *(stackElements.rbegin() + depth);

  LLVM_DEBUG({
    unsigned idx = Register::virtReg2Index(elem);
    dbgs() << "    Duplicating " << idx << " at depth " << depth << "\n";
  });

  stackElements.push_back(elem);
}

void EVMStackStatus::pop() {
  LLVM_DEBUG({
    unsigned reg = stackElements.back();
    unsigned idx = Register::virtReg2Index(reg);
    dbgs() << "    Popping %" << idx << " from stack.\n";
  });
  stackElements.pop_back();
}

void EVMStackStatus::push(unsigned reg) {
  LLVM_DEBUG({
    unsigned idx = Register::virtReg2Index(reg);
    dbgs() << "    Pushing %" << idx << " to top of stack.\n";
  });
  stackElements.push_back(reg);
}


void EVMStackStatus::dump() const {
  LLVM_DEBUG({
    dbgs() << "  Stack :  xRegion_size = " << getSizeOfXRegion() << "\n";
    unsigned counter = 0;
    for (auto i = stackElements.rbegin(), e = stackElements.rend(); i != e; ++i) {
      unsigned idx = Register::virtReg2Index(*i);
      dbgs() << "(" << counter << ", %" << idx << "), ";
      counter ++;
    }
    dbgs() << "\n";
  });
}

unsigned EVMStackStatus::findRegDepth(unsigned reg) const {
  unsigned curHeight = getStackDepth();

  for (unsigned d = 0; d < curHeight; ++d) {
    unsigned stackReg = get(d);
    if (stackReg == reg) {
      LLVM_DEBUG({
        dbgs() << "    Found %" << Register::virtReg2Index(reg)
               << " at depth: " << d << "\n";
      });
      return d;
    }
  }
  llvm_unreachable("Cannot find register on stack");
}


unsigned EdgeSets::getEdgeSetIndex(Edge edge) const {
  // Linear search. Can improve it.
  for (std::pair<unsigned, Edge> edgePair : edgeIndex) {
    if (edgePair.second == edge) {
      auto result = edgeIndex2EdgeSet.find(edgePair.first);
      assert(result != edgeIndex2EdgeSet.end() && "Cannot find edge set!");
      return result->second;
    }
  }
  llvm_unreachable("Cannot find edge index.");
}

unsigned EdgeSets::getEdgeIndex(Edge edge) const {
  // Linear search. Can improve it.
  for (std::pair<unsigned, Edge> edgePair : edgeIndex) {
    if (edgePair.second == edge) {
      return edgePair.first;
    }
  }
  llvm_unreachable("Cannot find edge index.");
}


void EdgeSets::collectEdges(MachineFunction *MF) {
  std::set<MachineBasicBlock*> visited;

  // Artifically create a {NULL, EntryMBB} Edge,
  // and {ExitMBB, NULL} Edge
  edgeIndex[0] = Edge(NULL, &MF->front());
  edgeIndex[1] = Edge(&MF->back(), NULL);

  for (MachineBasicBlock &MBB : *MF) {
    // Skip if we have visited this MBB:
    if (visited.find(&MBB) != visited.end()) {
      continue;
    }

    visited.insert(&MBB);
    for (auto *NextMBB : MBB.successors()) {
      unsigned key = edgeIndex.size();
      assert(edgeIndex.find(key) == edgeIndex.end() &&
             "Trying to emplace edge indexes.");
      edgeIndex[edgeIndex.size()] = Edge(&MBB, NextMBB);
    }
  }
}

void EdgeSets::computeEdgeSets(MachineFunction *MF) {
  // First, assign each edge with a number:
  collectEdges(MF);

  // Insert a default edge set: NULL->Entry:

  // Then, assign a new edge set for each of the edges
  for (std::pair<unsigned, Edge> index : edgeIndex) {
    edgeIndex2EdgeSet.insert({index.first, index.first});
  }

  // Criteria: Two MBBs share a same edge set if:
  // 1. they have a common child.
  // 2. they have a common parent.  
  for (MachineBasicBlock &MBB : *MF) {
    // we have more than one predecessors
    if (std::distance(MBB.pred_begin(), MBB.pred_end()) > 1) {
      // all predecessors belong to an edge set
      MachineBasicBlock *first = *MBB.pred_begin();

      MachineBasicBlock::pred_iterator iter = ++MBB.pred_begin();
      while (iter != MBB.pred_end()) {
        mergeEdgeSets({first, &MBB}, {*iter, &MBB});
      }
    }
    if (std::distance(MBB.succ_begin(), MBB.succ_end()) > 1) {
      // all sucessors belong to an edge set
      MachineBasicBlock *first = *MBB.succ_begin();

      MachineBasicBlock::pred_iterator iter = ++MBB.succ_begin();
      while (iter != MBB.succ_end()) {
        mergeEdgeSets({&MBB, first}, {&MBB, *iter});
      }
    }
  }
}

void EdgeSets::mergeEdgeSets(Edge edge1, Edge edge2) {
  unsigned edgeSet1 = getEdgeSetIndex(edge1);
  unsigned edgeSet2 = getEdgeSetIndex(edge2);

  // return if they are already in the same edge set.
  if (edgeSet1 == edgeSet2) {
    return;
  }

  // change edgeSet2 to edgeSet1
  for (std::pair<unsigned, unsigned> index2Set : edgeIndex2EdgeSet) {
    if (index2Set.second == edgeSet2) {
      edgeIndex2EdgeSet.emplace(index2Set.first, edgeSet1);
    }
  }
  return;
}

EdgeSets::Edge EdgeSets::getEdge(unsigned edgeId) const {
  auto result = edgeIndex.find(edgeId);
  assert(result != edgeIndex.end());
  return result->second;
}

void EdgeSets::dump() const {
  LLVM_DEBUG({
    dbgs() << "  Computed edge set: (size of edgesets: " << 0 << ")\n";
    for (std::pair<unsigned, unsigned> it : edgeIndex2EdgeSet) {
      // edge set Index : edge index
      unsigned edgeId = it.first;
      unsigned esIndex = it.second;
      // find the Edges
      Edge edge = getEdge(edgeId);

      if (edge.first == NULL) {
        dbgs() << "    EntryMBB"
               << " -> MBB" << edge.second->getNumber() << " : " << esIndex
               << "\n";
      } else if (edge.second == NULL) {
        dbgs() << "    MBB" << edge.first->getNumber() << " -> ExitMBB"
               << " : " << esIndex << "\n";
      } else {
        dbgs() << "    MBB" << edge.first->getNumber() << " -> MBB"
               << edge.second->getNumber() << " : " << esIndex << "\n";
      }
    }
    dbgs() << "-------------------------------------------------\n";
  });
}

void EVMStackAlloc::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LiveIntervals>();
  //AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void EVMStackAlloc::initialize() {
  edgeSets.reset();
  regAssignments.clear();
  currentStackStatus.reset();
  edgeset2assignment.clear();
}

void EVMStackAlloc::allocateRegistersToStack(MachineFunction &F) {
  // assert(MRI->isSSA() && "Must be run on SSA.");
  // clean up previous assignments.
  initialize();

  // compute edge sets
  edgeSets.computeEdgeSets(&F);
  edgeSets.dump();

  // analyze each BB
  for (MachineBasicBlock &MBB : F) {
    analyzeBasicBlock(&MBB);
  }

  // exit check: stack should be empty
}

static unsigned getDefRegister(const MachineInstr &MI) {
    unsigned numDefs = MI.getDesc().getNumDefs();
    assert(numDefs <= 1);
    const MachineOperand &def = *MI.defs().begin();
    assert(def.isReg() && def.isDef());
    return def.getReg();
}

bool EVMStackAlloc::defIsLocal(const MachineInstr &MI) const {
  unsigned defReg = getDefRegister(MI);

  // examine live range to see if it only covers a single MBB:
  const LiveInterval &LI = LIS->getInterval(defReg);
  // if it has multiple VNs, ignore it.
  if (LI.segments.size() > 1) {
    return false;
  }

  // if it goes across multiple MBBs, ignore it.
  const MachineBasicBlock* MBB = MI.getParent();
  SlotIndex MBBBegin = LIS->getMBBStartIdx(MBB);
  SlotIndex MBBEnd = LIS->getMBBEndIdx(MBB);

  return LI.isLocal(MBBBegin, MBBEnd);
}
void EVMStackAlloc::consolidateXRegionForEdgeSet(unsigned edgetSetIndex) {
  // TODO
  llvm_unreachable("unimplemented");
}

void EVMStackAlloc::beginOfBlockUpdates(MachineBasicBlock *MBB) {
  stack.clear();

  // find edgeset
  if (MBB->pred_empty()) {
    return;
  }
  MachineBasicBlock* Pred = *MBB->pred_begin();
  EdgeSets::Edge edge = {Pred, MBB};
  unsigned setIndex = edgeSets.getEdgeSetIndex(edge);

  assert(edgeset2assignment.find(setIndex) != edgeset2assignment.end());
  std::vector<unsigned> xStack = edgeset2assignment[setIndex];

  // initialize the stack using x stack.
  for (unsigned i = 0; i < xStack.size(); ++i) {
    stack.push(xStack[i]);
  }
}

void EVMStackAlloc::endOfBlockUpdates(MachineBasicBlock *MBB) {
  // make sure the reg is in X region.
  assert(stack.getStackDepth() == stack.getSizeOfXRegion() &&
         "L Region elements are still on the stack at end of MBB.");

  std::vector<unsigned> xStack(stack.getStackElements());

  for (MachineBasicBlock *NextMBB : MBB->successors()) {
    unsigned edgetSetIndex = edgeSets.getEdgeSetIndex({MBB, NextMBB});

    if (edgeset2assignment.find(edgetSetIndex) != edgeset2assignment.end()) {
      // We've already have an x stack assignment previously. so now we will
      // need to arrange them so they are the same
      
      std::vector<unsigned> &another = edgeset2assignment[edgetSetIndex];
      assert(another == xStack && "Two X Stack arrankkgements are different!");
      consolidateXRegionForEdgeSet(edgetSetIndex);
    }

    // TODO: merge edgeset
    edgeset2assignment.insert(
        std::pair<unsigned, std::vector<unsigned>>(edgetSetIndex, xStack));
  }
}

void EVMStackAlloc::analyzeBasicBlock(MachineBasicBlock *MBB) {
  LLVM_DEBUG({ dbgs() << "  Analyzing MBB" << MBB->getNumber() << ":\n"; });
  beginOfBlockUpdates(MBB);

  LLVM_DEBUG({
    dbgs() << "    X Stack dump: (size: " << stack.getStackDepth() << ") \n";
    stack.dump();
  });
  
  for (MachineBasicBlock::iterator I = MBB->begin(), E = MBB->end(); I != E;) {
    MachineInstr &MI = *I++;

    LLVM_DEBUG({ dbgs() << "  Instr: "; MI.dump();});

    // First consume, then create
    handleUses(MI);
    // rearrange operands
    // TODO


    handleDef(MI);

    stack.dump();
  }

  endOfBlockUpdates(MBB);
}

StackAssignment EVMStackAlloc::getStackAssignment(unsigned reg) const {
  assert(regAssignments.find(reg) != regAssignments.end() &&
         "Cannot find stack assignment for register.");
  return regAssignments.lookup(reg);
}

// Test whether Reg, as defined at Def, has exactly one use. This is a
// generalization of MachineRegisterInfo::hasOneUse that uses LiveIntervals
// to handle complex cases.
static bool hasOneUse(unsigned Reg, const MachineInstr *Def, MachineRegisterInfo *MRI,
                      LiveIntervals *LIS) {
  return MRI->hasOneUse(Reg);
  /*
  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MRI->hasOneUse(Reg))
    return true;

  bool HasOne = false;
  const LiveInterval &LI = LIS->getInterval(Reg);
  const VNInfo *DefVNI =
      LI.getVNInfoAt(LIS->getInstructionIndex(*Def).getRegSlot());
  assert(DefVNI);
  for (auto &I : MRI->use_nodbg_operands(Reg)) {
    const auto &Result = LI.Query(LIS->getInstructionIndex(*I.getParent()));
    if (Result.valueIn() == DefVNI) {
      if (!Result.isKill())
        return false;
      if (HasOne)
        return false;
      HasOne = true;
    }
  }
  return HasOne;
  */
}



// return true of the register use in the machine instruction is the last use.
bool EVMStackAlloc::regIsLastUse(const MachineOperand &MOP) const {
  assert(MOP.isReg());
  unsigned reg = MOP.getReg();

  if (hasOneUse(reg, MOP.getParent(), MRI, LIS))
    return true;
  
  // iterate over all uses
  const MachineInstr *MI = MOP.getParent();
  SlotIndex MISlot = LIS->getInstructionIndex(*MI).getRegSlot();
  SlotIndex BBEndSlot = LIS->getMBBEndIdx(MI->getParent());
  for (auto &Use : MRI->use_nodbg_operands(reg)) {
    MachineInstr * UseMI = Use.getParent();
    SlotIndex UseSlot = LIS->getInstructionIndex(*UseMI).getRegSlot();

    if (SlotIndex::isEarlierInstr(MISlot, UseSlot) &&
        SlotIndex::isEarlierInstr(UseSlot, BBEndSlot)) {
      return false;
    }
  }
  return true;
}

// return the number of uses of the register.
unsigned EVMStackAlloc::getRegNumUses(unsigned reg) const {
  return std::distance(MRI->use_nodbg_begin(reg), MRI->use_nodbg_end());
}

void EVMStackAlloc::handleDef(MachineInstr &MI) {
  if (MI.getNumDefs() == 0) {
    return;
  }

  unsigned defReg = getDefRegister(MI);

  // TODO: insert to SA
  StackAssignment sa;

  // case: multiple defs:
  // if there are multiple defines, then it goes to memory
  if (MRI->hasOneDef(defReg)) {
    // if the register has no use, then we do not allocate it
    if (MRI->use_nodbg_empty(defReg)) {
      regAssignments.insert(std::pair<unsigned, StackAssignment>(
          defReg, {NO_ALLOCATION, 0}));
      LLVM_DEBUG(dbgs() << "    Allocating reg %"
                        << Register::virtReg2Index(defReg)
                        << " to NO_ALLOCATION.\n");
      insertPopAfter(MI);
      return;
    }

    // LOCAL case
    if (defIsLocal(MI)) {
      // record assignment
      regAssignments.insert(
          std::pair<unsigned, StackAssignment>(defReg, {L_STACK, 0}));
      LLVM_DEBUG(dbgs() << "    Allocating reg %"
                        << Register::virtReg2Index(defReg)
                        << " to LOCAL STACK.\n");
      // update stack status
      currentStackStatus.L.insert(defReg);
      stack.push(defReg);
      return;
    }

    // If all uses are in a same edge set, send it to Transfer Stack
    // This could greatly benefit from a stack machine specific optimization.
    if (liveIntervalWithinSameEdgeSet(defReg)) {
      // it is a def register, so we only care about out-going edges.

      LLVM_DEBUG(dbgs() << "    Allocating reg %"
                        << Register::virtReg2Index(defReg)
                        << " to TRANSFER STACK.\n");

      // TODO: allocate X regions to the following code
      MachineBasicBlock *ThisMBB = const_cast<MachineInstr &>(MI).getParent();

      // find the outgoing edge set.
      assert(!ThisMBB->succ_empty() &&
             "Cannot allocate XRegion to empty edgeset.");
      unsigned edgesetIndex =
          edgeSets.getEdgeSetIndex({ThisMBB, *(ThisMBB->succ_begin())});
      allocateXRegion(edgesetIndex,defReg);

      stack.push(defReg);
      return;
    }
  } else {
    // Everything else goes to memory
    insertStoreToMemoryAfter(defReg, MI, sa.slot);
    currentStackStatus.M.insert(defReg);
    allocateMemorySlot(defReg);
    return;
  }
}

// We only look at uses.
bool EVMStackAlloc::liveIntervalWithinSameEdgeSet(unsigned defReg) {
  std::set<unsigned> edgeSetIndices;
  for (MachineOperand &use : MRI->use_operands(defReg)){
    MachineBasicBlock* MBB = use.getParent()->getParent();

    // Look for predecessor edges
    for (MachineBasicBlock* Pred : MBB->predecessors()) {
      EdgeSets::Edge edge = {Pred, MBB};
      unsigned setIndex = edgeSets.getEdgeSetIndex(edge);
      edgeSetIndices.insert(setIndex);
    }

    // Look for successor edges
    for (MachineBasicBlock* Succ : MBB->successors()) {
      EdgeSets::Edge edge = {MBB, Succ};
      unsigned setIndex = edgeSets.getEdgeSetIndex(edge);
      edgeSetIndices.insert(setIndex);
    }
  }

  assert(!edgeSetIndices.empty() && "Edge set cannot be empty.");

  if (edgeSetIndices.size() == 1) {
    return true;
  } else {
    return false;
  }
}

void EVMStackAlloc::handleUses(MachineInstr &MI) {

  // Collect reg use info
  std::vector<MOPUseType> useTypes;
  unsigned index = 0;
  for (const MachineOperand &MOP : MI.explicit_uses()) {
    if (!MOP.isReg()) {
      LLVM_DEBUG({
        if (MI.getOpcode() == EVM::PUSH32_r) {
          assert(MOP.isImm() || MOP.isCImm());
          assert(MI.getNumExplicitOperands() - MI.getNumExplicitDefs() == 1);
        } else if (MI.getOpcode() == EVM::JUMP_r ||
                   MI.getOpcode() == EVM::JUMPI_r) {
          assert(MOP.isSymbol() || MOP.isMBB());
        }
      });
      ++index;
      continue;
    }

    unsigned useReg = MOP.getReg();
    // get stack assignment
    assert(regAssignments.find(useReg) != regAssignments.end());
    StackAssignment SA = regAssignments.lookup(useReg);
    assert(SA.region != NO_ALLOCATION && "Cannot see unused register use.");

    bool isMemUse = false;
    bool isLastUse = false;
    if (regIsLastUse(MOP)) {
      isLastUse = true;
    }

    if (SA.region == NONSTACK) {
      // we need to handle memory types differently
      isMemUse = true;
    }
    unsigned depth = stack.findRegDepth(useReg);

    RegUseType RUT = {isLastUse, isMemUse, depth, useReg};
    useTypes.push_back({index, RUT});
    ++index;
  }

  LLVM_DEBUG({
    dbgs() << "      Uses: ";
    for (MOPUseType &MUT : useTypes) {
      dbgs() << " %" << Register::virtReg2Index(MUT.second.reg)
             << " index: " << MUT.first
             << ", last use: " << MUT.second.isLastUse << ", "
             << "isMemUse: " << MUT.second.isMemUse << ", "
             << "depth: " << MUT.second.stackDepth << "\n";
    }
  });

  // Handle uses (back to front), insert DUPs, SWAPs if needed
  unsigned numUses = index;
  switch (numUses) {
    case 1: {
      // handle unary operands
      assert(useTypes.size() == 1);
      handleUnaryOpcode(useTypes[0], MI);
      break;
    }
    case 2: {
      // handle binary operands
      assert(useTypes.size() == 2);
      handleBinaryOpcode(useTypes[0], useTypes[1], MI);
      break;
    }
    default: {
      // handle default operands
      assert(useTypes.size() > 2);
      handleArbitraryOpcode(useTypes, MI);
      break;
    }
  }
  
  

  for (MOPUseType &MUT : reverse(useTypes)) {
    unsigned index = MUT.first;
    unsigned reg = MUT.second.reg;
    StackAssignment SA = regAssignments.lookup(reg);
    if (MUT.second.isMemUse) {
      insertLoadFromMemoryBefore(reg, MI, SA.slot);
      stack.push(reg);
    }
    if (MUT.second.isLastUse) {
      if (MUT.second.isMemUse) {
        currentStackStatus.M.erase(reg);
        deallocateMemorySlot(reg);
      } else {
        // TODO: instead of SWAP, we might use DUP, and then clean up later
        insertSwapBefore(MUT.second.stackDepth, MI);
        if (SA.region == X_STACK) {
          currentStackStatus.X.erase(reg);
        }
      }
    }
  }

  

  // clean up 

}

void EVMStackAlloc::insertLoadFromMemoryBefore(unsigned reg, MachineInstr &MI,
                                               unsigned memSlot) {
  MachineInstrBuilder getlocal = 
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(EVM::pGETLOCAL_r), reg)
      .addImm(memSlot);
  LIS->InsertMachineInstrInMaps(*getlocal);
  stack.push(reg);

  LLVM_DEBUG(dbgs() << "  %" << Register::virtReg2Index(reg) << " <= GETLOCAL("
                    << memSlot << ") inserted.\n");
}

void EVMStackAlloc::insertDupBefore(unsigned index, MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineInstrBuilder dup = BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::DUP_r)).addImm(index);
  MBB->insert(MachineBasicBlock::iterator(MI), dup);
  LIS->InsertMachineInstrInMaps(*dup);
  stack.dup(index);
}

void EVMStackAlloc::insertStoreToMemoryAfter(unsigned reg, MachineInstr &MI, unsigned memSlot) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();

  MachineInstrBuilder putlocal =
      BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::pPUTLOCAL_r)).addReg(reg).addImm(memSlot);
  MBB->insertAfter(MachineBasicBlock::iterator(MI), putlocal);
  LIS->InsertMachineInstrInMaps(*putlocal);

  // TODO: insert this new put local to LiveIntervals

  LLVM_DEBUG(dbgs() << "  PUTLOCAL(" << memSlot << ") => %" << memSlot
                    << "  is inserted.\n");
}

void EVMStackAlloc::insertPopAfter(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineInstrBuilder pop = BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::POP_r));
  MBB->insertAfter(MachineBasicBlock::iterator(MI), pop);
  LIS->InsertMachineInstrInMaps(*pop);
  stack.pop();
}

void EVMStackAlloc::insertSwapBefore(unsigned index, MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineInstrBuilder swap =
      BuildMI(*MBB->getParent(), MI.getDebugLoc(), TII->get(EVM::SWAP_r))
          .addImm(index);
  MBB->insert(MachineBasicBlock::iterator(MI), swap);
  LIS->InsertMachineInstrInMaps(*swap);
  stack.swap(index);
}

bool EVMStackAlloc::handleSingleUse(MachineInstr &MI, const MachineOperand &MOP,
                                    std::vector<RegUseType> &usetypes) {
  if (!MOP.isReg()) {
    return false;
  }

  unsigned useReg = MOP.getReg();

  // get stack assignment
  assert(regAssignments.find(useReg) != regAssignments.end());
  StackAssignment SA = regAssignments.lookup(useReg);
  // we also do not care if we has determined we do not allocate it.
  if (SA.region == NO_ALLOCATION) {
    return false;
  }

  // update stack status if it is the last use.
  if (regIsLastUse(MOP)) {
    switch (SA.region) {
      case NONSTACK: {
        // release memory slot
        insertLoadFromMemoryBefore(useReg, MI, SA.slot);
        currentStackStatus.M.erase(useReg);
        deallocateMemorySlot(useReg);
        break;
      }
      case X_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertSwapBefore(depth, MI);
        currentStackStatus.X.erase(useReg);
        break;
      }
      case L_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertSwapBefore(depth, MI);
        break;
      }
      default: {
        llvm_unreachable("Impossible case");
        break;
      }
    }
  } else { // It is not the last use of a register.

    //assert(hasUsesAfterInSameBB(useReg, MI)/* || successorsHaveUses(useReg, MI)*/);
    switch (SA.region) {
      case NONSTACK: {
        // release memory slot
        insertLoadFromMemoryBefore(useReg, MI, SA.slot);
        stack.push(useReg);
        break;
      }
      case X_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertDupBefore(depth, MI);
        break;
      }
      case L_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertDupBefore(depth, MI);
        break;
      }
      default: {
        llvm_unreachable("Impossible case");
        break;
      }
    }
  }
  return true;
}

// return the allocated slot index of a memory
unsigned EVMStackAlloc::allocateMemorySlot(unsigned reg) {
  assert(reg != 0 && "Incoming registers cannot be zero.");
  // first, find if there is an empty slot:
  for (unsigned i = 0; i < memoryAssignment.size(); ++i) {
    // here we let 0 represent an empty slot
    if (memoryAssignment[i] == 0) {
      memoryAssignment[i] = reg;
      return i;
    }
  }

  // now we need to expand the memory:
  memoryAssignment.push_back(reg);
  return (memoryAssignment.size() - 1);
}

void EVMStackAlloc::deallocateMemorySlot(unsigned reg) {
  for (unsigned i = 0; i < memoryAssignment.size(); ++i) {
    if (reg == memoryAssignment[i]) {
      memoryAssignment[i] = 0;
      return;
    }
  }
  llvm_unreachable("Cannot find allocated memory slot");
}

unsigned EVMStackAlloc::allocateXRegion(unsigned setIndex, unsigned reg) {
  // Each entry of an edge set should contain the same
  // X region layout
  std::vector<unsigned> &x_region = edgeset2assignment[setIndex];

  assert(std::find(x_region.begin(), x_region.end(), reg) == x_region.end() &&
         "Inserting duplicate element in X region.");

  x_region.push_back(reg);
  return x_region.size();
}

bool EVMStackAlloc::hasUsesAfterInSameBB(unsigned reg, const MachineInstr &MI) const {
  const MachineBasicBlock* MBB = MI.getParent();

  // if this is the only use, then for sure it is the last use in MBB.
  assert(!MRI->use_nodbg_empty(reg) && "Empty use registers should not have a use.");
  if (MRI->hasOneUse(reg)) {
    return false;
  }

  SlotIndex FstUse = LIS->getInstructionIndex(MI);

  // iterate over uses and see if any use exists in the same BB.
  for (MachineRegisterInfo::use_nodbg_iterator 
       Use = MRI->use_nodbg_begin(reg),
       E = MRI->use_nodbg_end();
       Use != E; ++Use) {
    
    SlotIndex SI = LIS->getInstructionIndex(*Use->getParent());

    SlotIndex EndOfMBBSI = LIS->getMBBEndIdx(Use->getParent()->getParent());

    // Check if SI lies in between FstUSE and EndOfMBBSI
    if (SlotIndex::isEarlierInstr(FstUse, SI) &&
        SlotIndex::isEarlierInstr(SI, EndOfMBBSI)) {
      return true;
    }
  }

  // we cannot find a use after it in BB
  return false;
}

unsigned EVMStackAlloc::getCurrentStackDepth() const {
  return currentStackStatus.L.size() + currentStackStatus.X.size();
}

void EVMStackAlloc::pruneStackDepth() {
  unsigned stackDepth = getCurrentStackDepth();
  if (stackDepth < MAXIMUM_STACK_DEPTH) {
    return;
  }
  LLVM_DEBUG(dbgs() << "Stack Depth exceeds maximum, start pruning.");
  llvm_unreachable("unimplemented.");

  // First look at transfer stackœ:
  unsigned spillingCandidate = 0;
  std::set<unsigned> *vecRegs;
  if (currentStackStatus.X.size() != 0) {
    vecRegs = &currentStackStatus.X;
  } else {
    vecRegs = &currentStackStatus.L;
  }
  spillingCandidate = findSpillingCandidate(*vecRegs);
  allocateMemorySlot(spillingCandidate);
}

unsigned EVMStackAlloc::findSpillingCandidate(std::set<unsigned> &vecRegs) const {
  llvm_unreachable("unimplemented");
}

void EVMStackAlloc::getXStackRegion(unsigned edgeSetIndex,
                                    std::vector<unsigned> xRegion) const {
  // order is important
  assert(edgeset2assignment.find(edgeSetIndex) != edgeset2assignment.end() &&
         "Cannot find edgeset index!");
  llvm_unreachable("not implemented");
  //std::copy(edgeset2assignment.begin(), edgeset2assignment.end(), xRegion);
  return;
}

bool EVMStackAlloc::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<EVMSubtarget>().getInstrInfo(); 
  LIS = &getAnalysis<LiveIntervals>();
  MRI = &MF.getRegInfo();
  allocateRegistersToStack(MF);
  return true;
}

bool EVMStackAlloc::successorsHaveUses(unsigned reg, const MachineInstr &MI) const {
  const MachineBasicBlock* MBB = MI.getParent();

  // It is possible that we some NextMBB will not have uses. In this case we
  // probably will need to pop it at the end of liveInterval.

  for (const MachineBasicBlock* NextMBB : MBB->successors()) {
    llvm_unreachable("unimplemented");
  }

  return false;
}

void EVMStackAlloc::loadOperandFromMemoryIfNeeded(RegUseType op, MachineInstr &MI) {
  if (!op.isMemUse) {
    return;
  }

  StackAssignment SA = getStackAssignment(op.reg);
  assert(SA.region == NONSTACK);
  insertLoadFromMemoryBefore(op.reg, MI, SA.slot);
}

void EVMStackAlloc::handleOperandLiveness(RegUseType useType, MachineOperand &MOP) {
  assert(MOP.isReg() && useType.reg == MOP.getReg());

  unsigned reg = useType.reg;

  StackAssignment SA = getStackAssignment(reg);

  if (regIsLastUse(MOP)) {
    // when it goes out of liveness range, free resources if it is on memory or
    // X region.
    if (SA.region == NONSTACK) {
      currentStackStatus.M.erase(reg);
      deallocateMemorySlot(SA.slot);
    }
    if (SA.region == X_STACK) {
      currentStackStatus.X.erase(reg);
    }
  }
}

void EVMStackAlloc::handleUnaryOpcode(EVMStackAlloc::MOPUseType op, MachineInstr &MI) {
  assert(op.first == 0);

  unsigned reg = op.second.reg;
  MachineOperand &MOP = MI.getOperand(op.first + MI.getNumDefs());
  StackAssignment SA = getStackAssignment(reg);

  handleOperandLiveness(op.second, MOP);

  // if there is only one operand, simply bring it to the top.
  if (SA.region == NONSTACK) {
    insertLoadFromMemoryBefore(reg, MI, SA.slot);
  } else {
    unsigned depth = stack.findRegDepth(reg);
    if (regIsLastUse(MOP)) {
      insertSwapBefore(depth, MI);
    } else {
      insertDupBefore(depth, MI);
    }
  }
}

void EVMStackAlloc::handleBinaryOpcode(EVMStackAlloc::MOPUseType op1, MOPUseType op2,
                                       MachineInstr &MI) {
  assert(op1.first == 0 && op2.first == 1);

  handleOperandLiveness(op1.second, MI.getOperand(op1.first + MI.getNumDefs()));
  handleOperandLiveness(op2.second, MI.getOperand(op2.first + MI.getNumDefs()));

  unsigned reg1 = op1.second.reg;
  unsigned reg2 = op2.second.reg;
  StackAssignment SA1 = getStackAssignment(reg1);
  StackAssignment SA2 = getStackAssignment(reg2);

}

FunctionPass *llvm::createEVMStackAllocPass() {
  return new EVMStackAlloc();
}