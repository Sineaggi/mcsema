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
#include "x86Instrs_SETcc.h"

using namespace llvm;

static Value *doSetaV(InstPtr ip, BasicBlock *&b)
{

    // set 1 if CF==0 && ZF==0
    // else, set 0
    Value *cf_val = F_READ(b, "CF");
    Value *zf_val = F_READ(b, "ZF");

    // cf|zf == 0 iff cf == 0 && zf == 0
    Value   *f_or = 
        BinaryOperator::CreateOr(cf_val, zf_val, "", b);

    // to_set will be 1 iff cf == 0 && zf == 0
    Value *to_set = BinaryOperator::CreateNot(f_or, "", b);

    // zero extend to desired width
    Value   *ext_to_set = new ZExtInst(to_set, Type::getIntNTy(b->getContext(), 8), "", b);
    
    return ext_to_set;
}

static Value *doSetbV(InstPtr ip, BasicBlock *&b)
{
    // setb == set if cf == 1
    // essentially, read CF
    Value *cf_val = F_READ(b, "CF");

    // zero extend to desired width
    Value   *ext_to_set = new ZExtInst(cf_val, Type::getIntNTy(b->getContext(), 8), "", b);
    
    return ext_to_set;
}

static Value *doSetsV(InstPtr ip, BasicBlock *&b)
{
    // sets == set if sf == 1
    // essentially, read SF
    Value *sf_val = F_READ(b, "SF");

    // zero extend to desired width
    Value   *ext_to_set = new ZExtInst(sf_val, Type::getIntNTy(b->getContext(), 8), "", b);
    
    return ext_to_set;
}

static Value *doSetneV(InstPtr ip, BasicBlock *&b)
{
    //read ZF
    Value *zf_val = F_READ(b, "ZF");

    //compare to 0
    Value *cmp_res = 
      new ICmpInst(*b, CmpInst::ICMP_EQ, zf_val, CONST_V<1>(b,0));

    //extend result to 8 bits
    Value *res = new ZExtInst(cmp_res, Type::getInt8Ty(b->getContext()), "", b);

    //return
    return res;
}

static Value *doSeteV(InstPtr ip, BasicBlock *&b)
{
    //read ZF
    Value *zf_val = F_READ(b, "ZF");

    //compare to not 0
    Value *cmp_res = 
      new ICmpInst(*b, CmpInst::ICMP_NE, zf_val, CONST_V<1>(b,0));

    //extend result to 8 bits
    Value *res = new ZExtInst(cmp_res, Type::getInt8Ty(b->getContext()), "", b);

    //return
    return res;
}

// setge: of == sf
static Value *doSetgeV(InstPtr ip, BasicBlock *&b)
{
    Value *sf_val = F_READ(b, "SF");
    Value *of_val = F_READ(b, "OF");

    //compare of == sf
    Value *cmp_res = 
      new ICmpInst(*b, CmpInst::ICMP_EQ, sf_val, of_val);

    //extend result to 8 bits
    Value *res = new ZExtInst(cmp_res, Type::getInt8Ty(b->getContext()), "", b);

    //return
    return res;
}

// setg: of == sf && ZF==0
static Value *doSetgV(InstPtr ip, BasicBlock *&b)
{
    Value *sf_val = F_READ(b, "SF");
    Value *of_val = F_READ(b, "OF");
    Value *zf_val = F_READ(b, "ZF");

    //compare of == sf
    Value *cmp0_res = 
      new ICmpInst(*b, CmpInst::ICMP_EQ, sf_val, of_val);

    // if ZF == 0, NOT ZF == 1
    Value *not_zf = BinaryOperator::CreateNot(zf_val, "", b);

    // final result:
    // (sf==of) & (not zf)
    Value   *cmp_res = 
        BinaryOperator::CreateAnd(cmp0_res, not_zf, "", b);


    //extend result to 8 bits
    Value *res = new ZExtInst(cmp_res, Type::getInt8Ty(b->getContext()), "", b);

    //return
    return res;
}

// setle: of != sf && ZF==1
static Value *doSetleV(InstPtr ip, BasicBlock *&b)
{
    Value *sf_val = F_READ(b, "SF");
    Value *of_val = F_READ(b, "OF");
    Value *zf_val = F_READ(b, "ZF");

    //compare of == sf
    Value *cmp0_res = 
      new ICmpInst(*b, CmpInst::ICMP_NE, sf_val, of_val);

    // final result:
    // (sf!=of) & (zf)
    Value   *cmp_res = 
        BinaryOperator::CreateAnd(cmp0_res, zf_val, "", b);


    //extend result to 8 bits
    Value *res = new ZExtInst(cmp_res, Type::getInt8Ty(b->getContext()), "", b);

    //return
    return res;
}

// setge: of != sf
static Value *doSetlV(InstPtr ip, BasicBlock *&b)
{
    Value *sf_val = F_READ(b, "SF");
    Value *of_val = F_READ(b, "OF");

    //compare of != sf
    Value *cmp_res = 
      new ICmpInst(*b, CmpInst::ICMP_NE, sf_val, of_val);

    //extend result to 8 bits
    Value *res = new ZExtInst(cmp_res, Type::getInt8Ty(b->getContext()), "", b);

    //return
    return res;
}

// SETcc always operate on 8bit quantities
#define DO_SETCC_OP_REG(NAME)  static InstTransResult do ## NAME ## R(InstPtr ip, BasicBlock *&b, const MCOperand &reg) \
{ \
    TASSERT(reg.isReg(), "");\
    Value   *val_to_set = do ## NAME ## V(ip, b); \
    R_WRITE<8>(b, reg.getReg(), val_to_set); \
    return ContinueBlock; \
}

#define DO_SETCC_OP_MEM(NAME)  static InstTransResult do ## NAME ## M(InstPtr ip, BasicBlock *&b, Value *addr) \
{ \
    TASSERT(addr != NULL, ""); \
    Value   *val_to_set = do ## NAME ## V(ip ,b); \
    M_WRITE<8>(ip, b, addr, val_to_set); \
    return ContinueBlock; \
}

DO_SETCC_OP_REG(Seta)
DO_SETCC_OP_MEM(Seta)
DO_SETCC_OP_REG(Setb)
DO_SETCC_OP_MEM(Setb)
DO_SETCC_OP_REG(Setne)
DO_SETCC_OP_MEM(Setne)
DO_SETCC_OP_REG(Sete)
DO_SETCC_OP_MEM(Sete)
DO_SETCC_OP_REG(Setge)
DO_SETCC_OP_MEM(Setge)
DO_SETCC_OP_REG(Setg)
DO_SETCC_OP_MEM(Setg)
DO_SETCC_OP_REG(Setl)
DO_SETCC_OP_MEM(Setl)
DO_SETCC_OP_REG(Setle)
DO_SETCC_OP_MEM(Setle)
DO_SETCC_OP_REG(Sets)
DO_SETCC_OP_MEM(Sets)

GENERIC_TRANSLATION_MEM(SETAm, 
	doSetaM(ip, block, ADDR(0)),
	doSetaM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETAr, doSetaR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETBm, 
	doSetbM(ip, block, ADDR(0)),
	doSetbM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETBr, doSetbR(ip, block, OP(0)))
GENERIC_TRANSLATION(SETNEr, doSetneR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETNEm, 
	doSetneM(ip, block, ADDR(0)),
	doSetneM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETEr, doSeteR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETEm,
  doSeteM(ip, block, ADDR(0)),
  doSeteM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETGEr, doSetgeR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETGEm,
  doSetgeM(ip, block, ADDR(0)),
  doSetgeM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETGr, doSetgR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETGm,
  doSetgM(ip, block, ADDR(0)),
  doSetgM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETLr, doSetlR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETLm,
  doSetlM(ip, block, ADDR(0)),
  doSetlM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETLEr, doSetleR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETLEm,
  doSetleM(ip, block, ADDR(0)),
  doSetleM(ip, block, STD_GLOBAL_OP(0)))
GENERIC_TRANSLATION(SETSr, doSetsR(ip, block, OP(0)))
GENERIC_TRANSLATION_MEM(SETSm,
  doSetsM(ip, block, ADDR(0)),
  doSetsM(ip, block, STD_GLOBAL_OP(0)))

void SETcc_populateDispatchMap(DispatchMap &m) {
        m[X86::SETAm] = translate_SETAm;
        m[X86::SETAr] = translate_SETAr;
        m[X86::SETBm] = translate_SETBm;
        m[X86::SETBr] = translate_SETBr;
        m[X86::SETNEr] = translate_SETNEr;
        m[X86::SETNEm] = translate_SETNEm;
        m[X86::SETEr] = translate_SETEr;
        m[X86::SETEm] = translate_SETEm;
        m[X86::SETGEr] = translate_SETGEr;
        m[X86::SETGEm] = translate_SETGEm;
        m[X86::SETLr] = translate_SETLr;
        m[X86::SETLm] = translate_SETLm;
        m[X86::SETLEr] = translate_SETLEr;
        m[X86::SETLEm] = translate_SETLEm;
        m[X86::SETGr] = translate_SETGr;
        m[X86::SETGm] = translate_SETGm;
        m[X86::SETSr] = translate_SETSr;
        m[X86::SETSm] = translate_SETSm;
}
