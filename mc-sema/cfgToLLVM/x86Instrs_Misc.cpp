/*
Copyright (c) 2014, Trail of Bits
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

  Redistributions in binary form must reproduce the above copyright notice, this  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

  Neither the name of Trail of Bits nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "InstructionDispatch.h"
#include "toLLVM.h"
#include "X86.h"
#include "raiseX86.h"
#include "x86Helpers.h"
#include "x86Instrs_flagops.h"
#include "x86Instrs_Misc.h"

using namespace llvm;

static InstTransResult doNoop(BasicBlock *b) {
  //isn't this exciting
  return ContinueBlock;
}

static InstTransResult doInt3(BasicBlock *b) {
	Module	*M = b->getParent()->getParent();
	//emit an LLVM trap intrinsic
	//this should be changed to a debugtrap intrinsic eventually
	Function	*trapIntrin = Intrinsic::getDeclaration(M, Intrinsic::trap);

	CallInst::Create(trapIntrin, "", b);

    Value *unreachable = new UnreachableInst(b->getContext(), b);

	return ContinueBlock;
}


static InstTransResult doCdq( BasicBlock   *b ) {
    // EDX <- SEXT(EAX)

    //read EAX
    Value   *EAX_v = R_READ<32>(b, X86::EAX);

    //shift EAX_v to the right by 31
    Value   *c = CONST_V<32>(b, 31);
    Value   *EAX_sh = BinaryOperator::Create(Instruction::LShr, EAX_v, c, "", b);

    //truncate EAX_sh to 1 bit
    Value   *EAX_1 = new TruncInst( EAX_sh, 
                                    Type::getInt1Ty(b->getContext()), 
                                    "", 
                                    b);
    
    //sign-extend EAX_1 to a 32-bit value 
    Value   *EAX_sx = new SExtInst( EAX_1,
                                    Type::getInt32Ty(b->getContext()),
                                    "",
                                    b);
    //write this value to EDX
    R_WRITE<32>(b, X86::EDX, EAX_sx);

    //no flags are affected

    return ContinueBlock;
}


template <int width>
static InstTransResult doBswapR(InstPtr ip,   BasicBlock *&b,
                            const MCOperand &reg) 
{
    TASSERT(reg.isReg(), "");

    if(width != 32)
    {
        throw TErr(__LINE__, __FILE__, "Width not supported");
    }

    Value   *tmp = R_READ<width>(b, reg.getReg());

    // Create the new bytes from the original value
    Value   *newByte1 = BinaryOperator::CreateShl(tmp, CONST_V<width>(b, 24), "", b);
    Value   *newByte2 = BinaryOperator::CreateShl(BinaryOperator::CreateAnd(tmp, CONST_V<width>(b, 0x0000FF00), "", b),
                                                    CONST_V<width>(b, 8),
                                                    "",
                                                    b);
    Value   *newByte3 = BinaryOperator::CreateLShr(BinaryOperator::CreateAnd(tmp, CONST_V<width>(b, 0x00FF0000), "", b),
                                                    CONST_V<width>(b, 8),
                                                    "",
                                                    b);
    Value   *newByte4 = BinaryOperator::CreateLShr(tmp, CONST_V<width>(b, 24), "", b);
    
    // Add the bytes together to make the resulting DWORD
    Value   *res = BinaryOperator::CreateAdd(newByte1, newByte2, "", b);
    res = BinaryOperator::CreateAdd(res, newByte3, "", b);
    res = BinaryOperator::CreateAdd(res, newByte4, "", b);

    R_WRITE<width>(b, reg.getReg(), res);

    return ContinueBlock;
}


static InstTransResult doLAHF(BasicBlock *b) {

    //we need to create an 8-bit value out of the status 
    //flags, shift and OR them, and then write them into AH

    Type    *t = Type::getInt8Ty(b->getContext());
    Value   *cf = new ZExtInst(F_READ(b, "CF"), t, "", b);
    Value   *af = new ZExtInst(F_READ(b, "AF"), t, "", b);
    Value   *pf = new ZExtInst(F_READ(b, "PF"), t, "", b);
    Value   *zf = new ZExtInst(F_READ(b, "ZF"), t, "", b);
    Value   *sf = new ZExtInst(F_READ(b, "SF"), t, "", b);

    //shift everything
    Value   *p_0 = cf;
    Value   *p_1 = 
        BinaryOperator::CreateShl(CONST_V<8>(b, 1), CONST_V<8>(b, 1), "", b);
    Value   *p_2 = 
        BinaryOperator::CreateShl(pf, CONST_V<8>(b, 2), "", b);
    Value   *p_3 =
        BinaryOperator::CreateShl(CONST_V<8>(b, 0), CONST_V<8>(b, 3), "", b);
    Value   *p_4 =
        BinaryOperator::CreateShl(af, CONST_V<8>(b, 4), "", b);
    Value   *p_5 =
        BinaryOperator::CreateShl(CONST_V<8>(b, 0), CONST_V<8>(b, 5), "", b);
    Value   *p_6 =
        BinaryOperator::CreateShl(zf, CONST_V<8>(b, 6), "", b);
    Value   *p_7 =
        BinaryOperator::CreateShl(sf, CONST_V<8>(b, 7), "", b);

    //OR everything
    Value   *res = 
        BinaryOperator::CreateOr(
            BinaryOperator::CreateOr(
                BinaryOperator::CreateOr(
                    BinaryOperator::CreateOr(p_0, p_1, "", b), 
                    p_2, "", b), 
                p_3, "", b), 
            BinaryOperator::CreateOr(
                BinaryOperator::CreateOr(
                    BinaryOperator::CreateOr(p_4, p_5, "", b), 
                    p_6, "", b), 
                p_7, "", b), 
            "", b);

    R_WRITE<8>(b, X86::AH, res);

    return ContinueBlock;
}

static InstTransResult doStd(BasicBlock *b) {

    F_SET(b, "DF");

    return ContinueBlock;
}

static InstTransResult doCld(BasicBlock *b) {

    F_CLEAR(b, "DF");

    return ContinueBlock;
}

static InstTransResult doStc(BasicBlock *b) {

    F_SET(b, "CF");

    return ContinueBlock;
}

static InstTransResult doClc(BasicBlock *b) {

    F_CLEAR(b, "CF");

    return ContinueBlock;
}


template<int width>
static InstTransResult doLea(InstPtr ip,   BasicBlock *&b, 
                        Value           *addr,
                        const MCOperand &dst)
{
    // LEA <r>, <expr>
    TASSERT(addr != NULL, "");
    TASSERT(dst.isReg(), "");

    //addr is an address, so, convert it to an integer value to write
    Type    *ty = Type::getIntNTy(b->getContext(), width);
    Value   *addrInt = new PtrToIntInst(addr, ty, "", b);

    //write the address into the register
    R_WRITE<width>(b, dst.getReg(), addrInt);

    return ContinueBlock;
}

static InstTransResult doRdtsc(BasicBlock *b) {
  /* write out a call to the RDTSC intrinsic */
	Module	*M = b->getParent()->getParent();
	//emit an LLVM trap intrinsic
	//this should be changed to a debugtrap intrinsic eventually
	Function	*rcc = Intrinsic::getDeclaration(M, Intrinsic::readcyclecounter);

	CallInst::Create(rcc, "", b);

  return ContinueBlock;
}

static InstTransResult doAAA(BasicBlock *b) {

    Function    *F = b->getParent();
    //trueBlock for when ((AL & 0x0F > 9) || (AF == 1)); falseblock otherwise
    BasicBlock  *trueBlock = BasicBlock::Create(F->getContext(), "", F);
    BasicBlock  *falseBlock = BasicBlock::Create(F->getContext(), "", F);
    BasicBlock  *endBlock = BasicBlock::Create(F->getContext(), "", F);

    Value   *al;
    Value   *af;

    al = R_READ<8>(b, X86::AL);
    af = F_READ(b, "AF");

    // AL & 0x0F 
    Value   *andRes = BinaryOperator::CreateAnd(al, CONST_V<8>(b, 0x0F), "", b);

    // ((AL & 0x0F) > 9)? 
    Value   *testRes = new ICmpInst(*b,
                                    CmpInst::ICMP_UGT,
                                    andRes,
                                    CONST_V<8>(b, 9));

    Value   *orRes = BinaryOperator::CreateOr(testRes, af, "", b);

    BranchInst::Create(trueBlock, falseBlock, orRes, b);

    //True Block Statements
    Value   *alRes = BinaryOperator::CreateAdd(al, CONST_V<8>(trueBlock, 6), "", trueBlock);
    R_WRITE<8>(trueBlock, X86::AL, alRes);

    Value   *ahRes = BinaryOperator::CreateAdd(R_READ<8>(trueBlock, X86::AH), CONST_V<8>(trueBlock, 1), "", trueBlock);
    R_WRITE<8>(trueBlock, X86::AH, ahRes);

    F_SET(trueBlock, "AF");
    F_SET(trueBlock, "CF");

    alRes = BinaryOperator::CreateAnd(alRes, CONST_V<8>(trueBlock, 0x0F), "", trueBlock);
    R_WRITE<8>(trueBlock, X86::AL, alRes);

    BranchInst::Create(endBlock, trueBlock);

    //False Block Statements
    F_CLEAR(falseBlock, "AF");
    F_CLEAR(falseBlock, "CF");

    alRes = BinaryOperator::CreateAnd(al, CONST_V<8>(trueBlock, 0x0F), "", falseBlock);
    R_WRITE<8>(falseBlock, X86::AL, alRes);

    BranchInst::Create(endBlock, falseBlock);

    F_ZAP(endBlock, "OF");
    F_ZAP(endBlock, "SF");
    F_ZAP(endBlock, "ZF");
    F_ZAP(endBlock, "PF");

    //update our parents concept of what the current block is
    b = endBlock;

    return ContinueBlock;
}

static InstTransResult doAAS(BasicBlock *b) {

    Function    *F = b->getParent();
    //trueBlock for when ((AL & 0x0F > 9) || (AF == 1)); falseblock otherwise
    BasicBlock  *trueBlock = BasicBlock::Create(F->getContext(), "", F);
    BasicBlock  *falseBlock = BasicBlock::Create(F->getContext(), "", F);
    BasicBlock  *endBlock = BasicBlock::Create(F->getContext(), "", F);

    Value   *al;
    Value   *af;

    al = R_READ<8>(b, X86::AL);
    af = F_READ(b, "AF");

    // AL & 0x0F 
    Value   *andRes = BinaryOperator::CreateAnd(al, CONST_V<8>(b, 0x0F), "", b);

    // ((AL & 0x0F) > 9)? 
    Value   *testRes = new ICmpInst(*b,
                                    CmpInst::ICMP_UGT,
                                    andRes,
                                    CONST_V<8>(b, 9));

    Value   *orRes = BinaryOperator::CreateOr(testRes, af, "", b);

    BranchInst::Create(trueBlock, falseBlock, orRes, b);

    //True Block Statements
    Value   *alRes = BinaryOperator::CreateSub(al, CONST_V<8>(trueBlock, 6), "", trueBlock);
    R_WRITE<8>(trueBlock, X86::AL, alRes);

    Value   *ahRes = BinaryOperator::CreateSub(R_READ<8>(trueBlock, X86::AH), CONST_V<8>(trueBlock, 1), "", trueBlock);
    R_WRITE<8>(trueBlock, X86::AH, ahRes);

    F_SET(trueBlock, "AF");
    F_SET(trueBlock, "CF");

    alRes = BinaryOperator::CreateAnd(alRes, CONST_V<8>(trueBlock, 0x0F), "", trueBlock);
    R_WRITE<8>(trueBlock, X86::AL, alRes);

    BranchInst::Create(endBlock, trueBlock);

    //False Block Statements
    F_CLEAR(falseBlock, "AF");
    F_CLEAR(falseBlock, "CF");

    alRes = BinaryOperator::CreateAnd(al, CONST_V<8>(trueBlock, 0x0F), "", falseBlock);
    R_WRITE<8>(falseBlock, X86::AL, alRes);

    BranchInst::Create(endBlock, falseBlock);

    F_ZAP(endBlock, "OF");
    F_ZAP(endBlock, "SF");
    F_ZAP(endBlock, "ZF");
    F_ZAP(endBlock, "PF");

    //update our parents concept of what the current block is
    b = endBlock;

    return ContinueBlock;
}

static InstTransResult doAAM(BasicBlock *b) {

    Value   *al;

    al = R_READ<8>(b, X86::AL);

    Value   *res = BinaryOperator::Create(Instruction::SDiv, al, CONST_V<8>(b, 0x0A), "", b);
    Value   *mod = BinaryOperator::Create(Instruction::SRem, al, CONST_V<8>(b, 0x0A), "", b);

    R_WRITE<8>(b, X86::AL, mod);
    R_WRITE<8>(b, X86::AH, res);

    WriteSF<8>(b, mod);
    WriteZF<8>(b, mod);
    WritePF<8>(b, mod);
    F_ZAP(b, "OF");
    F_ZAP(b, "AF");
    F_ZAP(b, "CF");

    return ContinueBlock;
}

static InstTransResult doAAD(BasicBlock *b) {

    Value   *al;
    Value   *ah;

    al = R_READ<8>(b, X86::AL);
    ah = R_READ<8>(b, X86::AH);

    Value   *tmp = BinaryOperator::Create(Instruction::Mul, ah, CONST_V<8>(b, 0x0A), "", b);
    tmp = BinaryOperator::CreateAdd(tmp, al, "", b);
    tmp = BinaryOperator::CreateAnd(tmp, CONST_V<8>(b, 0xFF), "", b);

    R_WRITE<8>(b, X86::AL, tmp);
    R_WRITE<8>(b, X86::AH, CONST_V<8>(b, 0x00));

    WriteSF<8>(b, tmp);
    WriteZF<8>(b, tmp);
    WritePF<8>(b, tmp);
    F_ZAP(b, "OF");
    F_ZAP(b, "AF");
    F_ZAP(b, "CF");

    return ContinueBlock;
}

template <int width>
static InstTransResult doCwd(BasicBlock *b) {

    // read ax or eax
    Value *ax_val = R_READ<width>(b, X86::EAX);

    // sign extend to twice width
    Type    *dt = Type::getIntNTy(b->getContext(), width*2);
    Value   *tmp = new SExtInst(ax_val, dt, "", b);

    // rotate leftmost bits into rightmost
    Type    *t = Type::getIntNTy(b->getContext(), width);
    Value   *res_sh = BinaryOperator::Create(
                Instruction::LShr, 
                tmp, 
                CONST_V<width*2>(b, width), 
                "", 
                b);
    // original rightmost
    Value   *wrAX = new TruncInst(tmp, t, "", b);
    // original leftmost
    Value   *wrDX = new TruncInst(res_sh, t, "", b);
    switch(width) {
        case 16:
            R_WRITE<width>(b, X86::DX, wrDX);
            R_WRITE<width>(b, X86::AX, wrAX);
            break;
        case 32:
            R_WRITE<width>(b, X86::EDX, wrDX);
            R_WRITE<width>(b, X86::EAX, wrAX);
            break;
        default:
            throw TErr(__LINE__, __FILE__, "Not supported width");
    }

    return ContinueBlock;

}
GENERIC_TRANSLATION(CDQ, doCdq(block))
GENERIC_TRANSLATION(INT3, doInt3(block))
GENERIC_TRANSLATION(NOOP, doNoop(block))

GENERIC_TRANSLATION(BSWAP32r, doBswapR<32>(ip, block, OP(0)))

GENERIC_TRANSLATION(LAHF, doLAHF(block))
GENERIC_TRANSLATION(STD, doStd(block))
GENERIC_TRANSLATION(CLD, doCld(block))
GENERIC_TRANSLATION(STC, doStc(block))
GENERIC_TRANSLATION(CLC, doClc(block))

GENERIC_TRANSLATION_MEM(LEA16r, 
	doLea<16>(ip, block, ADDR(1), OP(0)),
	doLea<16>(ip, block, STD_GLOBAL_OP(1), OP(0))) 
GENERIC_TRANSLATION_MEM(LEA32r, 
	doLea<32>(ip, block, ADDR(1), OP(0)),
	doLea<32>(ip, block, STD_GLOBAL_OP(1), OP(0))) 

GENERIC_TRANSLATION(AAA, doAAA(block))
GENERIC_TRANSLATION(AAS, doAAS(block))
GENERIC_TRANSLATION(AAM8i8, doAAM(block))
GENERIC_TRANSLATION(AAD8i8, doAAD(block))
GENERIC_TRANSLATION(RDTSC, doRdtsc(block))
GENERIC_TRANSLATION(CWD, doCwd<16>(block))
GENERIC_TRANSLATION(CWDE, doCwd<32>(block))

void Misc_populateDispatchMap(DispatchMap &m) {
    m[X86::AAA] = translate_AAA;
    m[X86::AAS] = translate_AAS;
    m[X86::AAM8i8] = translate_AAM8i8;
    m[X86::AAD8i8] = translate_AAD8i8;
    m[X86::LEA16r] = translate_LEA16r;
    m[X86::LEA32r] = translate_LEA32r;
    m[X86::LAHF] = translate_LAHF;
    m[X86::STD] = translate_STD;
    m[X86::CLD] = translate_CLD;
    m[X86::STC] = translate_STC;
    m[X86::CLC] = translate_CLC;
    m[X86::BSWAP32r] = translate_BSWAP32r;
    m[X86::CDQ] = translate_CDQ;
    m[X86::INT3] = translate_INT3;
    m[X86::NOOP] = translate_NOOP;
    m[X86::LOCK_PREFIX] = translate_NOOP;
    m[X86::PAUSE] = translate_NOOP;
    m[X86::RDTSC] = translate_RDTSC;
    m[X86::CWD] = translate_CWD;
    m[X86::CWDE] = translate_CWDE;
}
