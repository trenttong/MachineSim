/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2011 Intel Corporation. All rights reserved.

Written by Xin Tong, University of Toronto.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

#ifndef _TTC_H
#define _TTC_H

#include "branch_predictor.H"

/// @ TARGETCACHE - this class implements a tagless target cache
//  @ indirect branch predictor. 
//  @ in target cache indirect branch predictor, branch addresses
//  @ are xor'ed with the history information. and then indexed into
//  @ the branch history table.
//  @ history information can be branch pattern history, it can 
//  @ also be branch path history.
//  @ In this implementation. a branch path history is used.
//  @
//  @ for more information. please refer to 
//  @ Target Prediction for Indirect Jump Paper by Yale Patt.
class TARGETCACHE : public BRANCHPREDICTOR 
{
private:
  enum 
  {
    TABLESIZEBIT = 13,
    TABLESIZE = 1<<TABLESIZEBIT,
  };

  static VOID Instruction(INS ins, VOID *v);
  static VOID IndirBranch(TARGETCACHE *tc, ADDRINT ip, BOOL taken, ADDRINT addr, THREADID tid);
  inline ADDRINT *HistAddress(ADDRINT ip, THREADID tid);
public:

  /* ===================================================================== */
  /* Target Cache predictor BPT                                            */
  /* ===================================================================== */
  TargetCacheThreadInfo PredictorData[MAX_PREDICTOR_THREAD]; 

  /// @ Activate - register the instruction callback.
  VOID Activate()
  {
    INS_AddInstrumentFunction(Instruction, this);
  }
};

inline VOID TARGETCACHE::Instruction(INS ins, VOID *v)
{
  TARGETCACHE *tc = static_cast<TARGETCACHE*>(v);

  static INT32 NopCount = 0;
  tc->InitChannelAddr(INS_IsNop(ins), NopCount, INS_Address(ins));

  if (INS_IsIndirectBranchOrCall(ins) && !INS_IsCall(ins) && !INS_IsRet(ins))
     {
     INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                              (AFUNPTR)IndirBranch, 
                              IARG_PTR, (void *)tc, 
                              IARG_PTR, INS_Address(ins),
                              IARG_BRANCH_TAKEN, 
                              IARG_BRANCH_TARGET_ADDR, 
                              IARG_THREAD_ID,
                              IARG_END);
     }
}

inline ADDRINT * TARGETCACHE::HistAddress(ADDRINT ip, THREADID tid)
{
  ASSERTX(TABLESIZE % 2 == 0);

  ADDRINT* InfoBranchHist = (ADDRINT*) PredictorData[tid].InfoBranchHist;
  UINT64 InfoGlobalHist = PredictorData[tid].InfoGlobalHist;

  ADDRINT* addr = &(InfoBranchHist[(ip & (TABLESIZE -1)) ^ InfoGlobalHist]);

  ASSERTX(addr >= &InfoBranchHist[0]);
  ASSERTX(addr <= &InfoBranchHist[TABLESIZE-1]);
  return addr;
}

inline VOID TARGETCACHE::IndirBranch(TARGETCACHE *tc, ADDRINT ip, BOOL taken, ADDRINT addr, THREADID tid)
{
  // Single thread profile enabled.
  if (tc->GetSingleOSThreadID() && PIN_GetTid()!=tc->GetSingleOSThreadID()) return; 

  // get the Info entry.
  TargetCacheThreadInfo *Info = &tc->PredictorData[tid];
  Info->Setup(TABLESIZE, sizeof(ADDRINT));

  if (tc->IsInterpBranch(ip))
     {
     Info->InfoInterpReferences ++;
     }

  // record this prediction.
  Info->InfoReferences++;

  ADDRINT *BranchHistEntry = tc->HistAddress(ip, tid);
  if (*BranchHistEntry == addr) 
     {
     Info->InfoPredicts ++;
     if (tc->IsInterpBranch(ip))
        {
        Info->InfoInterpPredicts ++;
        }
     }

  // updates the branch history table.
  *BranchHistEntry = addr;
  // updates the branch path table.
  Info->InfoGlobalHist <<= 2; 
  Info->InfoGlobalHist |= (addr & 0x3);
  Info->InfoGlobalHist &= (TABLESIZE - 1); 
}

#endif // _TTC_H
