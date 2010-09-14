//===-- ARMFastISel.cpp - ARM FastISel implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ARM-specific support for the FastISel class. Some
// of the target-specific code is generated by tablegen in the file
// ARMGenFastISel.inc, which is #included here.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMCallingConv.h"
#include "ARMRegisterInfo.h"
#include "ARMTargetMachine.h"
#include "ARMSubtarget.h"
#include "llvm/CallingConv.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/FastISel.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

static cl::opt<bool>
EnableARMFastISel("arm-fast-isel",
                  cl::desc("Turn on experimental ARM fast-isel support"),
                  cl::init(false), cl::Hidden);

namespace {

class ARMFastISel : public FastISel {

  /// Subtarget - Keep a pointer to the ARMSubtarget around so that we can
  /// make the right decision when generating code for different targets.
  const ARMSubtarget *Subtarget;
  const TargetMachine &TM;
  const TargetInstrInfo &TII;
  const TargetLowering &TLI;
  const ARMFunctionInfo *AFI;

  // Convenience variable to avoid checking all the time.
  bool isThumb;

  public:
    explicit ARMFastISel(FunctionLoweringInfo &funcInfo)
    : FastISel(funcInfo),
      TM(funcInfo.MF->getTarget()),
      TII(*TM.getInstrInfo()),
      TLI(*TM.getTargetLowering()) {
      Subtarget = &TM.getSubtarget<ARMSubtarget>();
      AFI = funcInfo.MF->getInfo<ARMFunctionInfo>();
      isThumb = AFI->isThumbFunction();
    }

    // Code from FastISel.cpp.
    virtual unsigned FastEmitInst_(unsigned MachineInstOpcode,
                                   const TargetRegisterClass *RC);
    virtual unsigned FastEmitInst_r(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC,
                                    unsigned Op0, bool Op0IsKill);
    virtual unsigned FastEmitInst_rr(unsigned MachineInstOpcode,
                                     const TargetRegisterClass *RC,
                                     unsigned Op0, bool Op0IsKill,
                                     unsigned Op1, bool Op1IsKill);
    virtual unsigned FastEmitInst_ri(unsigned MachineInstOpcode,
                                     const TargetRegisterClass *RC,
                                     unsigned Op0, bool Op0IsKill,
                                     uint64_t Imm);
    virtual unsigned FastEmitInst_rf(unsigned MachineInstOpcode,
                                     const TargetRegisterClass *RC,
                                     unsigned Op0, bool Op0IsKill,
                                     const ConstantFP *FPImm);
    virtual unsigned FastEmitInst_i(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC,
                                    uint64_t Imm);
    virtual unsigned FastEmitInst_rri(unsigned MachineInstOpcode,
                                      const TargetRegisterClass *RC,
                                      unsigned Op0, bool Op0IsKill,
                                      unsigned Op1, bool Op1IsKill,
                                      uint64_t Imm);
    virtual unsigned FastEmitInst_extractsubreg(MVT RetVT,
                                                unsigned Op0, bool Op0IsKill,
                                                uint32_t Idx);

    // Backend specific FastISel code.
    virtual bool TargetSelectInstruction(const Instruction *I);
    virtual unsigned TargetMaterializeConstant(const Constant *C);

  #include "ARMGenFastISel.inc"

    // Instruction selection routines.
  private:
    virtual bool ARMSelectLoad(const Instruction *I);
    virtual bool ARMSelectStore(const Instruction *I);
    virtual bool ARMSelectBranch(const Instruction *I);
    virtual bool ARMSelectCmp(const Instruction *I);
    virtual bool ARMSelectFPExt(const Instruction *I);
    virtual bool ARMSelectFPTrunc(const Instruction *I);
    virtual bool ARMSelectBinaryOp(const Instruction *I, unsigned ISDOpcode);
    virtual bool ARMSelectSIToFP(const Instruction *I);
    virtual bool ARMSelectFPToSI(const Instruction *I);
    virtual bool ARMSelectSDiv(const Instruction *I);

    // Utility routines.
  private:
    bool isTypeLegal(const Type *Ty, EVT &VT);
    bool isLoadTypeLegal(const Type *Ty, EVT &VT);
    bool ARMEmitLoad(EVT VT, unsigned &ResultReg, unsigned Reg, int Offset);
    bool ARMEmitStore(EVT VT, unsigned SrcReg, unsigned Reg, int Offset);
    bool ARMLoadAlloca(const Instruction *I, EVT VT);
    bool ARMStoreAlloca(const Instruction *I, unsigned SrcReg, EVT VT);
    bool ARMComputeRegOffset(const Value *Obj, unsigned &Reg, int &Offset);
    unsigned ARMMaterializeFP(const ConstantFP *CFP, EVT VT);
    unsigned ARMMaterializeInt(const Constant *C);
    unsigned ARMMoveToFPReg(EVT VT, unsigned SrcReg);
    unsigned ARMMoveToIntReg(EVT VT, unsigned SrcReg);

    // Call handling routines.
  private:
    CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool Return);
    bool ARMEmitLibcall(const Instruction *I, Function *F);

    // OptionalDef handling routines.
  private:
    bool DefinesOptionalPredicate(MachineInstr *MI, bool *CPSR);
    const MachineInstrBuilder &AddOptionalDefs(const MachineInstrBuilder &MIB);
};

} // end anonymous namespace

#include "ARMGenCallingConv.inc"

// DefinesOptionalPredicate - This is different from DefinesPredicate in that
// we don't care about implicit defs here, just places we'll need to add a
// default CCReg argument. Sets CPSR if we're setting CPSR instead of CCR.
bool ARMFastISel::DefinesOptionalPredicate(MachineInstr *MI, bool *CPSR) {
  const TargetInstrDesc &TID = MI->getDesc();
  if (!TID.hasOptionalDef())
    return false;

  // Look to see if our OptionalDef is defining CPSR or CCR.
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || !MO.isDef()) continue;
    if (MO.getReg() == ARM::CPSR)
      *CPSR = true;
  }
  return true;
}

// If the machine is predicable go ahead and add the predicate operands, if
// it needs default CC operands add those.
const MachineInstrBuilder &
ARMFastISel::AddOptionalDefs(const MachineInstrBuilder &MIB) {
  MachineInstr *MI = &*MIB;

  // Do we use a predicate?
  if (TII.isPredicable(MI))
    AddDefaultPred(MIB);

  // Do we optionally set a predicate?  Preds is size > 0 iff the predicate
  // defines CPSR. All other OptionalDefines in ARM are the CCR register.
  bool CPSR = false;
  if (DefinesOptionalPredicate(MI, &CPSR)) {
    if (CPSR)
      AddDefaultT1CC(MIB);
    else
      AddDefaultCC(MIB);
  }
  return MIB;
}

unsigned ARMFastISel::FastEmitInst_(unsigned MachineInstOpcode,
                                    const TargetRegisterClass* RC) {
  unsigned ResultReg = createResultReg(RC);
  const TargetInstrDesc &II = TII.get(MachineInstOpcode);

  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, ResultReg));
  return ResultReg;
}

unsigned ARMFastISel::FastEmitInst_r(unsigned MachineInstOpcode,
                                     const TargetRegisterClass *RC,
                                     unsigned Op0, bool Op0IsKill) {
  unsigned ResultReg = createResultReg(RC);
  const TargetInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, ResultReg)
                   .addReg(Op0, Op0IsKill * RegState::Kill));
  else {
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
                   .addReg(Op0, Op0IsKill * RegState::Kill));
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                   TII.get(TargetOpcode::COPY), ResultReg)
                   .addReg(II.ImplicitDefs[0]));
  }
  return ResultReg;
}

unsigned ARMFastISel::FastEmitInst_rr(unsigned MachineInstOpcode,
                                      const TargetRegisterClass *RC,
                                      unsigned Op0, bool Op0IsKill,
                                      unsigned Op1, bool Op1IsKill) {
  unsigned ResultReg = createResultReg(RC);
  const TargetInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, ResultReg)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addReg(Op1, Op1IsKill * RegState::Kill));
  else {
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addReg(Op1, Op1IsKill * RegState::Kill));
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                           TII.get(TargetOpcode::COPY), ResultReg)
                   .addReg(II.ImplicitDefs[0]));
  }
  return ResultReg;
}

unsigned ARMFastISel::FastEmitInst_ri(unsigned MachineInstOpcode,
                                      const TargetRegisterClass *RC,
                                      unsigned Op0, bool Op0IsKill,
                                      uint64_t Imm) {
  unsigned ResultReg = createResultReg(RC);
  const TargetInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, ResultReg)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addImm(Imm));
  else {
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addImm(Imm));
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                           TII.get(TargetOpcode::COPY), ResultReg)
                   .addReg(II.ImplicitDefs[0]));
  }
  return ResultReg;
}

unsigned ARMFastISel::FastEmitInst_rf(unsigned MachineInstOpcode,
                                      const TargetRegisterClass *RC,
                                      unsigned Op0, bool Op0IsKill,
                                      const ConstantFP *FPImm) {
  unsigned ResultReg = createResultReg(RC);
  const TargetInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, ResultReg)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addFPImm(FPImm));
  else {
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addFPImm(FPImm));
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                           TII.get(TargetOpcode::COPY), ResultReg)
                   .addReg(II.ImplicitDefs[0]));
  }
  return ResultReg;
}

unsigned ARMFastISel::FastEmitInst_rri(unsigned MachineInstOpcode,
                                       const TargetRegisterClass *RC,
                                       unsigned Op0, bool Op0IsKill,
                                       unsigned Op1, bool Op1IsKill,
                                       uint64_t Imm) {
  unsigned ResultReg = createResultReg(RC);
  const TargetInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, ResultReg)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addReg(Op1, Op1IsKill * RegState::Kill)
                   .addImm(Imm));
  else {
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
                   .addReg(Op0, Op0IsKill * RegState::Kill)
                   .addReg(Op1, Op1IsKill * RegState::Kill)
                   .addImm(Imm));
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                           TII.get(TargetOpcode::COPY), ResultReg)
                   .addReg(II.ImplicitDefs[0]));
  }
  return ResultReg;
}

unsigned ARMFastISel::FastEmitInst_i(unsigned MachineInstOpcode,
                                     const TargetRegisterClass *RC,
                                     uint64_t Imm) {
  unsigned ResultReg = createResultReg(RC);
  const TargetInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, ResultReg)
                   .addImm(Imm));
  else {
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
                   .addImm(Imm));
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                           TII.get(TargetOpcode::COPY), ResultReg)
                   .addReg(II.ImplicitDefs[0]));
  }
  return ResultReg;
}

unsigned ARMFastISel::FastEmitInst_extractsubreg(MVT RetVT,
                                                 unsigned Op0, bool Op0IsKill,
                                                 uint32_t Idx) {
  unsigned ResultReg = createResultReg(TLI.getRegClassFor(RetVT));
  assert(TargetRegisterInfo::isVirtualRegister(Op0) &&
         "Cannot yet extract from physregs");
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt,
                         DL, TII.get(TargetOpcode::COPY), ResultReg)
                 .addReg(Op0, getKillRegState(Op0IsKill), Idx));
  return ResultReg;
}

// TODO: Don't worry about 64-bit now, but when this is fixed remove the
// checks from the various callers.
unsigned ARMFastISel::ARMMoveToFPReg(EVT VT, unsigned SrcReg) {
  if (VT.getSimpleVT().SimpleTy == MVT::f64) return 0;
  
  unsigned MoveReg = createResultReg(TLI.getRegClassFor(VT));
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                          TII.get(ARM::VMOVRS), MoveReg)
                  .addReg(SrcReg));
  return MoveReg;
}

unsigned ARMFastISel::ARMMoveToIntReg(EVT VT, unsigned SrcReg) {
  if (VT.getSimpleVT().SimpleTy == MVT::i64) return 0;
  
  unsigned MoveReg = createResultReg(TLI.getRegClassFor(VT));
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                          TII.get(ARM::VMOVSR), MoveReg)
                  .addReg(SrcReg));
  return MoveReg;
}

// For double width floating point we need to materialize two constants
// (the high and the low) into integer registers then use a move to get
// the combined constant into an FP reg.
unsigned ARMFastISel::ARMMaterializeFP(const ConstantFP *CFP, EVT VT) {
  const APFloat Val = CFP->getValueAPF();
  bool is64bit = VT.getSimpleVT().SimpleTy == MVT::f64;

  // This checks to see if we can use VFP3 instructions to materialize
  // a constant, otherwise we have to go through the constant pool.
  if (TLI.isFPImmLegal(Val, VT)) {
    unsigned Opc = is64bit ? ARM::FCONSTD : ARM::FCONSTS;
    unsigned DestReg = createResultReg(TLI.getRegClassFor(VT));
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(Opc),
                            DestReg)
                    .addFPImm(CFP));
    return DestReg;
  }
  
  // Require VFP2 for loading fp constants.
  if (!Subtarget->hasVFP2()) return false;
  
  // MachineConstantPool wants an explicit alignment.
  unsigned Align = TD.getPrefTypeAlignment(CFP->getType());
  if (Align == 0) {
    // TODO: Figure out if this is correct.
    Align = TD.getTypeAllocSize(CFP->getType());
  }
  unsigned Idx = MCP.getConstantPoolIndex(cast<Constant>(CFP), Align);
  unsigned DestReg = createResultReg(TLI.getRegClassFor(VT));
  unsigned Opc = is64bit ? ARM::VLDRD : ARM::VLDRS;
  
  // The extra reg is for addrmode5.
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(Opc))
                  .addReg(DestReg).addConstantPoolIndex(Idx)
                  .addReg(0));
  return DestReg;
}

// TODO: Verify 64-bit.
unsigned ARMFastISel::ARMMaterializeInt(const Constant *C) {
  // MachineConstantPool wants an explicit alignment.
  unsigned Align = TD.getPrefTypeAlignment(C->getType());
  if (Align == 0) {
    // TODO: Figure out if this is correct.
    Align = TD.getTypeAllocSize(C->getType());
  }
  unsigned Idx = MCP.getConstantPoolIndex(C, Align);
  unsigned DestReg = createResultReg(TLI.getRegClassFor(MVT::i32));
  
  if (isThumb)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                            TII.get(ARM::t2LDRpci))
                    .addReg(DestReg).addConstantPoolIndex(Idx));
  else
    // The extra reg and immediate are for addrmode2.
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                            TII.get(ARM::LDRcp))
                            .addReg(DestReg).addConstantPoolIndex(Idx)
                    .addReg(0).addImm(0));

  return DestReg;
}

unsigned ARMFastISel::TargetMaterializeConstant(const Constant *C) {
  EVT VT = TLI.getValueType(C->getType(), true);

  // Only handle simple types.
  if (!VT.isSimple()) return 0;

  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(C))
    return ARMMaterializeFP(CFP, VT);
  return ARMMaterializeInt(C);
}

bool ARMFastISel::isTypeLegal(const Type *Ty, EVT &VT) {
  VT = TLI.getValueType(Ty, true);

  // Only handle simple types.
  if (VT == MVT::Other || !VT.isSimple()) return false;

  // Handle all legal types, i.e. a register that will directly hold this
  // value.
  return TLI.isTypeLegal(VT);
}

bool ARMFastISel::isLoadTypeLegal(const Type *Ty, EVT &VT) {
  if (isTypeLegal(Ty, VT)) return true;

  // If this is a type than can be sign or zero-extended to a basic operation
  // go ahead and accept it now.
  if (VT == MVT::i8 || VT == MVT::i16)
    return true;

  return false;
}

// Computes the Reg+Offset to get to an object.
bool ARMFastISel::ARMComputeRegOffset(const Value *Obj, unsigned &Reg,
                                      int &Offset) {
  // Some boilerplate from the X86 FastISel.
  const User *U = NULL;
  unsigned Opcode = Instruction::UserOp1;
  if (const Instruction *I = dyn_cast<Instruction>(Obj)) {
    // Don't walk into other basic blocks; it's possible we haven't
    // visited them yet, so the instructions may not yet be assigned
    // virtual registers.
    if (FuncInfo.MBBMap[I->getParent()] != FuncInfo.MBB)
      return false;
    Opcode = I->getOpcode();
    U = I;
  } else if (const ConstantExpr *C = dyn_cast<ConstantExpr>(Obj)) {
    Opcode = C->getOpcode();
    U = C;
  }

  if (const PointerType *Ty = dyn_cast<PointerType>(Obj->getType()))
    if (Ty->getAddressSpace() > 255)
      // Fast instruction selection doesn't support the special
      // address spaces.
      return false;

  switch (Opcode) {
    default:
    break;
    case Instruction::Alloca: {
      assert(false && "Alloca should have been handled earlier!");
      return false;
    }
  }

  // FIXME: Handle global variables.
  if (const GlobalValue *GV = dyn_cast<GlobalValue>(Obj)) {
    (void)GV;
    return false;
  }

  // Try to get this in a register if nothing else has worked.
  Reg = getRegForValue(Obj);
  if (Reg == 0) return false;

  // Since the offset may be too large for the load instruction
  // get the reg+offset into a register.
  // TODO: Verify the additions work, otherwise we'll need to add the
  // offset instead of 0 to the instructions and do all sorts of operand
  // munging.
  // TODO: Optimize this somewhat.
  if (Offset != 0) {
    ARMCC::CondCodes Pred = ARMCC::AL;
    unsigned PredReg = 0;

    if (!isThumb)
      emitARMRegPlusImmediate(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                              Reg, Reg, Offset, Pred, PredReg,
                              static_cast<const ARMBaseInstrInfo&>(TII));
    else {
      assert(AFI->isThumb2Function());
      emitT2RegPlusImmediate(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                             Reg, Reg, Offset, Pred, PredReg,
                             static_cast<const ARMBaseInstrInfo&>(TII));
    }
  }
  return true;
}

bool ARMFastISel::ARMLoadAlloca(const Instruction *I, EVT VT) {
  Value *Op0 = I->getOperand(0);

  // Verify it's an alloca.
  if (const AllocaInst *AI = dyn_cast<AllocaInst>(Op0)) {
    DenseMap<const AllocaInst*, int>::iterator SI =
      FuncInfo.StaticAllocaMap.find(AI);

    if (SI != FuncInfo.StaticAllocaMap.end()) {
      TargetRegisterClass* RC = TLI.getRegClassFor(VT);
      unsigned ResultReg = createResultReg(RC);
      TII.loadRegFromStackSlot(*FuncInfo.MBB, *FuncInfo.InsertPt,
                               ResultReg, SI->second, RC,
                               TM.getRegisterInfo());
      UpdateValueMap(I, ResultReg);
      return true;
    }
  }
  return false;
}

bool ARMFastISel::ARMEmitLoad(EVT VT, unsigned &ResultReg,
                              unsigned Reg, int Offset) {

  assert(VT.isSimple() && "Non-simple types are invalid here!");
  unsigned Opc;

  switch (VT.getSimpleVT().SimpleTy) {
    default:
      assert(false && "Trying to emit for an unhandled type!");
      return false;
    case MVT::i16:
      Opc = isThumb ? ARM::tLDRH : ARM::LDRH;
      VT = MVT::i32;
      break;
    case MVT::i8:
      Opc = isThumb ? ARM::tLDRB : ARM::LDRB;
      VT = MVT::i32;
      break;
    case MVT::i32:
      Opc = isThumb ? ARM::tLDR : ARM::LDR;
      break;
  }

  ResultReg = createResultReg(TLI.getRegClassFor(VT));

  // TODO: Fix the Addressing modes so that these can share some code.
  // Since this is a Thumb1 load this will work in Thumb1 or 2 mode.
  if (isThumb)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                            TII.get(Opc), ResultReg)
                    .addReg(Reg).addImm(Offset).addReg(0));
  else
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                            TII.get(Opc), ResultReg)
                    .addReg(Reg).addReg(0).addImm(Offset));
  return true;
}

bool ARMFastISel::ARMSelectLoad(const Instruction *I) {
  // Verify we have a legal type before going any further.
  EVT VT;
  if (!isLoadTypeLegal(I->getType(), VT))
    return false;

  // If we're an alloca we know we have a frame index and can emit the load
  // directly in short order.
  if (ARMLoadAlloca(I, VT))
    return true;

  // Our register and offset with innocuous defaults.
  unsigned Reg = 0;
  int Offset = 0;

  // See if we can handle this as Reg + Offset
  if (!ARMComputeRegOffset(I->getOperand(0), Reg, Offset))
    return false;

  unsigned ResultReg;
  if (!ARMEmitLoad(VT, ResultReg, Reg, Offset /* 0 */)) return false;

  UpdateValueMap(I, ResultReg);
  return true;
}

bool ARMFastISel::ARMStoreAlloca(const Instruction *I, unsigned SrcReg, EVT VT){
  Value *Op1 = I->getOperand(1);

  // Verify it's an alloca.
  if (const AllocaInst *AI = dyn_cast<AllocaInst>(Op1)) {
    DenseMap<const AllocaInst*, int>::iterator SI =
      FuncInfo.StaticAllocaMap.find(AI);

    if (SI != FuncInfo.StaticAllocaMap.end()) {
      TargetRegisterClass* RC = TLI.getRegClassFor(VT);
      assert(SrcReg != 0 && "Nothing to store!");
      TII.storeRegToStackSlot(*FuncInfo.MBB, *FuncInfo.InsertPt,
                              SrcReg, true /*isKill*/, SI->second, RC,
                              TM.getRegisterInfo());
      return true;
    }
  }
  return false;
}

bool ARMFastISel::ARMEmitStore(EVT VT, unsigned SrcReg,
                               unsigned DstReg, int Offset) {
  unsigned StrOpc;
  switch (VT.getSimpleVT().SimpleTy) {
    default: return false;
    case MVT::i1:
    case MVT::i8: StrOpc = isThumb ? ARM::tSTRB : ARM::STRB; break;
    case MVT::i16: StrOpc = isThumb ? ARM::tSTRH : ARM::STRH; break;
    case MVT::i32: StrOpc = isThumb ? ARM::tSTR : ARM::STR; break;
    case MVT::f32:
      if (!Subtarget->hasVFP2()) return false;
      StrOpc = ARM::VSTRS;
      break;
    case MVT::f64:
      if (!Subtarget->hasVFP2()) return false;
      StrOpc = ARM::VSTRD;
      break;
  }

  if (isThumb)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                            TII.get(StrOpc), SrcReg)
                    .addReg(DstReg).addImm(Offset).addReg(0));
  else
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                            TII.get(StrOpc), SrcReg)
                    .addReg(DstReg).addReg(0).addImm(Offset));

  return true;
}

bool ARMFastISel::ARMSelectStore(const Instruction *I) {
  Value *Op0 = I->getOperand(0);
  unsigned SrcReg = 0;

  // Yay type legalization
  EVT VT;
  if (!isLoadTypeLegal(I->getOperand(0)->getType(), VT))
    return false;

  // Get the value to be stored into a register.
  SrcReg = getRegForValue(Op0);
  if (SrcReg == 0)
    return false;

  // If we're an alloca we know we have a frame index and can emit the store
  // quickly.
  if (ARMStoreAlloca(I, SrcReg, VT))
    return true;

  // Our register and offset with innocuous defaults.
  unsigned Reg = 0;
  int Offset = 0;

  // See if we can handle this as Reg + Offset
  if (!ARMComputeRegOffset(I->getOperand(1), Reg, Offset))
    return false;

  if (!ARMEmitStore(VT, SrcReg, Reg, Offset /* 0 */)) return false;

  return false;
}

bool ARMFastISel::ARMSelectBranch(const Instruction *I) {
  const BranchInst *BI = cast<BranchInst>(I);
  MachineBasicBlock *TBB = FuncInfo.MBBMap[BI->getSuccessor(0)];
  MachineBasicBlock *FBB = FuncInfo.MBBMap[BI->getSuccessor(1)];

  // Simple branch support.
  unsigned CondReg = getRegForValue(BI->getCondition());
  if (CondReg == 0) return false;

  unsigned CmpOpc = isThumb ? ARM::t2CMPrr : ARM::CMPrr;
  unsigned BrOpc = isThumb ? ARM::t2Bcc : ARM::Bcc;
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(CmpOpc))
                  .addReg(CondReg).addReg(CondReg));
  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(BrOpc))
                  .addMBB(TBB).addImm(ARMCC::NE).addReg(ARM::CPSR);
  FastEmitBranch(FBB, DL);
  FuncInfo.MBB->addSuccessor(TBB);
  return true;
}

bool ARMFastISel::ARMSelectCmp(const Instruction *I) {
  const CmpInst *CI = cast<CmpInst>(I);

  EVT VT;
  const Type *Ty = CI->getOperand(0)->getType();
  if (!isTypeLegal(Ty, VT))
    return false;

  bool isFloat = (Ty->isDoubleTy() || Ty->isFloatTy());
  if (isFloat && !Subtarget->hasVFP2())
    return false;

  unsigned CmpOpc;
  switch (VT.getSimpleVT().SimpleTy) {
    default: return false;
    // TODO: Verify compares.
    case MVT::f32:
      CmpOpc = ARM::VCMPES;
      break;
    case MVT::f64:
      CmpOpc = ARM::VCMPED;
      break;
    case MVT::i32:
      CmpOpc = isThumb ? ARM::t2CMPrr : ARM::CMPrr;
      break;
  }

  unsigned Arg1 = getRegForValue(CI->getOperand(0));
  if (Arg1 == 0) return false;

  unsigned Arg2 = getRegForValue(CI->getOperand(1));
  if (Arg2 == 0) return false;

  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(CmpOpc))
                  .addReg(Arg1).addReg(Arg2));

  // For floating point we need to move the result to a comparison register
  // that we can then use for branches.
  if (isFloat)
    AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                            TII.get(ARM::FMSTAT)));

  // TODO: How to update the value map when there's no result reg?
  return true;
}

bool ARMFastISel::ARMSelectFPExt(const Instruction *I) {
  // Make sure we have VFP and that we're extending float to double.
  if (!Subtarget->hasVFP2()) return false;

  Value *V = I->getOperand(0);
  if (!I->getType()->isDoubleTy() ||
      !V->getType()->isFloatTy()) return false;

  unsigned Op = getRegForValue(V);
  if (Op == 0) return false;

  unsigned Result = createResultReg(ARM::DPRRegisterClass);
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                          TII.get(ARM::VCVTDS), Result)
                  .addReg(Op));
  UpdateValueMap(I, Result);
  return true;
}

bool ARMFastISel::ARMSelectFPTrunc(const Instruction *I) {
  // Make sure we have VFP and that we're truncating double to float.
  if (!Subtarget->hasVFP2()) return false;

  Value *V = I->getOperand(0);
  if (!I->getType()->isFloatTy() ||
      !V->getType()->isDoubleTy()) return false;

  unsigned Op = getRegForValue(V);
  if (Op == 0) return false;

  unsigned Result = createResultReg(ARM::SPRRegisterClass);
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                          TII.get(ARM::VCVTSD), Result)
                  .addReg(Op));
  UpdateValueMap(I, Result);
  return true;
}

bool ARMFastISel::ARMSelectSIToFP(const Instruction *I) {
  // Make sure we have VFP.
  if (!Subtarget->hasVFP2()) return false;
  
  EVT DstVT;
  const Type *Ty = I->getType();
  if (!isTypeLegal(Ty, DstVT))
    return false;
  
  unsigned Op = getRegForValue(I->getOperand(0));
  if (Op == 0) return false;
  
  // The conversion routine works on fp-reg to fp-reg and the operand above
  // was an integer, move it to the fp registers if possible.
  unsigned FP = ARMMoveToFPReg(DstVT, Op);
  if (FP == 0) return false;
  
  unsigned Opc;
  if (Ty->isFloatTy()) Opc = ARM::VSITOS;
  else if (Ty->isDoubleTy()) Opc = ARM::VSITOD;
  else return 0;
  
  unsigned ResultReg = createResultReg(TLI.getRegClassFor(DstVT));
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(Opc),
                          ResultReg)
                  .addReg(FP));
  UpdateValueMap(I, ResultReg);
  return true;
}

bool ARMFastISel::ARMSelectFPToSI(const Instruction *I) {
  // Make sure we have VFP.
  if (!Subtarget->hasVFP2()) return false;
  
  EVT DstVT;
  const Type *RetTy = I->getType();
  if (!isTypeLegal(RetTy, DstVT))
    return false;
  
  unsigned Op = getRegForValue(I->getOperand(0));
  if (Op == 0) return false;
  
  unsigned Opc;
  const Type *OpTy = I->getOperand(0)->getType();
  if (OpTy->isFloatTy()) Opc = ARM::VTOSIZS;
  else if (OpTy->isDoubleTy()) Opc = ARM::VTOSIZD;
  else return 0;
  EVT OpVT = TLI.getValueType(OpTy, true);
  
  unsigned ResultReg = createResultReg(TLI.getRegClassFor(OpVT));
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(Opc),
                          ResultReg)
                  .addReg(Op));
        
  // This result needs to be in an integer register, but the conversion only
  // takes place in fp-regs.
  unsigned IntReg = ARMMoveToIntReg(DstVT, ResultReg);
  if (IntReg == 0) return false;
  
  UpdateValueMap(I, IntReg);
  return true;
}

bool ARMFastISel::ARMSelectBinaryOp(const Instruction *I, unsigned ISDOpcode) {
  EVT VT  = TLI.getValueType(I->getType(), true);

  // We can get here in the case when we want to use NEON for our fp
  // operations, but can't figure out how to. Just use the vfp instructions
  // if we have them.
  // FIXME: It'd be nice to use NEON instructions.
  const Type *Ty = I->getType();
  bool isFloat = (Ty->isDoubleTy() || Ty->isFloatTy());
  if (isFloat && !Subtarget->hasVFP2())
    return false;

  unsigned Op1 = getRegForValue(I->getOperand(0));
  if (Op1 == 0) return false;

  unsigned Op2 = getRegForValue(I->getOperand(1));
  if (Op2 == 0) return false;

  unsigned Opc;
  bool is64bit = VT.getSimpleVT().SimpleTy == MVT::f64 ||
                 VT.getSimpleVT().SimpleTy == MVT::i64;
  switch (ISDOpcode) {
    default: return false;
    case ISD::FADD:
      Opc = is64bit ? ARM::VADDD : ARM::VADDS;
      break;
    case ISD::FSUB:
      Opc = is64bit ? ARM::VSUBD : ARM::VSUBS;
      break;
    case ISD::FMUL:
      Opc = is64bit ? ARM::VMULD : ARM::VMULS;
      break;
  }
  unsigned ResultReg = createResultReg(TLI.getRegClassFor(VT));
  AddOptionalDefs(BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
                          TII.get(Opc), ResultReg)
                  .addReg(Op1).addReg(Op2));
  UpdateValueMap(I, ResultReg);
  return true;
}

// Call Handling Code

// This is largely taken directly from CCAssignFnForNode - we don't support
// varargs in FastISel so that part has been removed.
// TODO: We may not support all of this.
CCAssignFn *ARMFastISel::CCAssignFnForCall(CallingConv::ID CC, bool Return) {
  switch (CC) {
  default:
    llvm_unreachable("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    // Use target triple & subtarget features to do actual dispatch.
    if (Subtarget->isAAPCS_ABI()) {
      if (Subtarget->hasVFP2() &&
          FloatABIType == FloatABI::Hard)
        return (Return ? RetCC_ARM_AAPCS_VFP: CC_ARM_AAPCS_VFP);
      else
        return (Return ? RetCC_ARM_AAPCS: CC_ARM_AAPCS);
    } else
        return (Return ? RetCC_ARM_APCS: CC_ARM_APCS);
  case CallingConv::ARM_AAPCS_VFP:
    return (Return ? RetCC_ARM_AAPCS_VFP: CC_ARM_AAPCS_VFP);
  case CallingConv::ARM_AAPCS:
    return (Return ? RetCC_ARM_AAPCS: CC_ARM_AAPCS);
  case CallingConv::ARM_APCS:
    return (Return ? RetCC_ARM_APCS: CC_ARM_APCS);
  }
}

// A quick function that will emit a call for a named libcall in F with the
// vector of passed arguments for the Instruction in I. We can assume that we
// can emit a call for any libcall we can produce. This is an abridged version 
// of the full call infrastructure since we won't need to worry about things 
// like computed function pointers or strange arguments at call sites.
// TODO: Try to unify this and the normal call bits for ARM, then try to unify
// with X86.
bool ARMFastISel::ARMEmitLibcall(const Instruction *I, Function *F) {
  CallingConv::ID CC = F->getCallingConv();
  
  // Handle *simple* calls for now.
  const Type *RetTy = F->getReturnType();
  EVT RetVT;
  if (RetTy->isVoidTy())
    RetVT = MVT::isVoid;
  else if (!isTypeLegal(RetTy, RetVT))
    return false;
  
  assert(!F->isVarArg() && "Vararg libcall?!");

  // Abridged from the X86 FastISel call selection mechanism
  SmallVector<Value*, 8> Args;
  SmallVector<unsigned, 8> ArgRegs;
  SmallVector<EVT, 8> ArgVTs;
  SmallVector<ISD::ArgFlagsTy, 8> ArgFlags;
  Args.reserve(I->getNumOperands());
  ArgRegs.reserve(I->getNumOperands());
  ArgVTs.reserve(I->getNumOperands());
  ArgFlags.reserve(I->getNumOperands());
  for (unsigned i = 0; i < Args.size(); ++i) {
    Value *Op = I->getOperand(i);
    unsigned Arg = getRegForValue(Op);
    if (Arg == 0) return false;
    
    const Type *ArgTy = Op->getType();
    EVT ArgVT;
    if (!isTypeLegal(ArgTy, ArgVT)) return false;
    
    ISD::ArgFlagsTy Flags;
    unsigned OriginalAlignment = TD.getABITypeAlignment(ArgTy);
    Flags.setOrigAlign(OriginalAlignment);
    
    Args.push_back(Op);
    ArgRegs.push_back(Arg);
    ArgVTs.push_back(ArgVT);
    ArgFlags.push_back(Flags);
  }
  
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CC, false, TM, ArgLocs, F->getContext());
  CCInfo.AnalyzeCallOperands(ArgVTs, ArgFlags, CCAssignFnForCall(CC, false));
  
  // Process the args.
  SmallVector<unsigned, 4> RegArgs;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    unsigned Arg = ArgRegs[VA.getValNo()];
    EVT ArgVT = ArgVTs[VA.getValNo()];
    
    // Should we ever have to promote?
    switch (VA.getLocInfo()) {
      case CCValAssign::Full: break;
      default:
        assert(false && "Handle arg promotion for libcalls?");
        return false;
    }
    
    // Now copy/store arg to correct locations.
    if (VA.isRegLoc()) {
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(TargetOpcode::COPY),
        VA.getLocReg()).addReg(Arg);
      RegArgs.push_back(VA.getLocReg());
    } else {
      // Need to store
      return false;
    }
  }
  
  // Issue the call, BLr9 for darwin, BL otherwise.
  MachineInstrBuilder MIB;
  unsigned CallOpc = Subtarget->isTargetDarwin() ? ARM::BLr9 : ARM::BL;
  MIB = BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(CallOpc))
        .addGlobalAddress(F, 0, 0);
  
  // Add implicit physical register uses to the call.
  for (unsigned i = 0, e = RegArgs.size(); i != e; ++i)
    MIB.addReg(RegArgs[i]);
    
  // Now the return value.
  SmallVector<unsigned, 4> UsedRegs;
  if (RetVT.getSimpleVT().SimpleTy != MVT::isVoid) {
    SmallVector<CCValAssign, 16> RVLocs;
    CCState CCInfo(CC, false, TM, RVLocs, F->getContext());
    CCInfo.AnalyzeCallResult(RetVT, CCAssignFnForCall(CC, true));

    // Copy all of the result registers out of their specified physreg.
    assert(RVLocs.size() == 1 && "Can't handle multi-value calls!");
    EVT CopyVT = RVLocs[0].getValVT();
    TargetRegisterClass* DstRC = TLI.getRegClassFor(CopyVT);
    
    unsigned ResultReg = createResultReg(DstRC);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(TargetOpcode::COPY),
            ResultReg).addReg(RVLocs[0].getLocReg());
    UsedRegs.push_back(RVLocs[0].getLocReg());
    
    // Finally update the result.        
    UpdateValueMap(I, ResultReg);
  }
  
  // Set all unused physreg defs as dead.
  static_cast<MachineInstr *>(MIB)->setPhysRegsDeadExcept(UsedRegs, TRI);

  return true;
}

bool ARMFastISel::ARMSelectSDiv(const Instruction *I) {
  EVT VT;
  const Type *Ty = I->getType();
  if (!isTypeLegal(Ty, VT))
    return false;
    
  // If we have integer div support we should have gotten already, emit a
  // libcall.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (VT == MVT::i16)
    LC = RTLIB::SDIV_I16;
  else if (VT == MVT::i32)
    LC = RTLIB::SDIV_I32;
  else if (VT == MVT::i64)
    LC = RTLIB::SDIV_I64;
  else if (VT == MVT::i128)
    LC = RTLIB::SDIV_I128;
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported SDIV!");
  
  // Binary operand with all the same type.
  std::vector<const Type*> ArgTys;
  ArgTys.push_back(Ty);
  ArgTys.push_back(Ty);
  const FunctionType *FTy = FunctionType::get(Ty, ArgTys, false);
  Function *F = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                 TLI.getLibcallName(LC));
  if (Subtarget->isAAPCS_ABI())
    F->setCallingConv(CallingConv::ARM_AAPCS);
  else
    F->setCallingConv(I->getParent()->getParent()->getCallingConv());
  
  return ARMEmitLibcall(I, F);
}

// TODO: SoftFP support.
bool ARMFastISel::TargetSelectInstruction(const Instruction *I) {
  // No Thumb-1 for now.
  if (isThumb && !AFI->isThumb2Function()) return false;

  switch (I->getOpcode()) {
    case Instruction::Load:
      return ARMSelectLoad(I);
    case Instruction::Store:
      return ARMSelectStore(I);
    case Instruction::Br:
      return ARMSelectBranch(I);
    case Instruction::ICmp:
    case Instruction::FCmp:
      return ARMSelectCmp(I);
    case Instruction::FPExt:
      return ARMSelectFPExt(I);
    case Instruction::FPTrunc:
      return ARMSelectFPTrunc(I);
    case Instruction::SIToFP:
      return ARMSelectSIToFP(I);
    case Instruction::FPToSI:
      return ARMSelectFPToSI(I);
    case Instruction::FAdd:
      return ARMSelectBinaryOp(I, ISD::FADD);
    case Instruction::FSub:
      return ARMSelectBinaryOp(I, ISD::FSUB);
    case Instruction::FMul:
      return ARMSelectBinaryOp(I, ISD::FMUL);
    case Instruction::SDiv:
      return ARMSelectSDiv(I);
    default: break;
  }
  return false;
}

namespace llvm {
  llvm::FastISel *ARM::createFastISel(FunctionLoweringInfo &funcInfo) {
    if (EnableARMFastISel) return new ARMFastISel(funcInfo);
    return 0;
  }
}
