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

#ifndef _BIMODAL_H
#define _BIMODAL_H

#include "branch_predictor.H"

/// @ BIMODAL - this class implements a BIMODAL branch predictor. 
//  @ A bimodal predictor is a state machine with four states:
//  @
//  @ - Strongly not taken
//  @ - Weakly not taken
//  @ - Weakly taken
//  @ - Strongly taken
//  @
//  @ When a branch is evaluated, the corresponding state machine is updated. 
//  @ Branches evaluated as not taken decrement the state towards strongly not taken, 
//  @ and branches evaluated as taken increment the state towards strongly taken. 
//  @ The advantage of the two-bit counter over a one-bit scheme is that a conditional 
//  @ jump has to deviate twice from what it has done most in the past before the 
//  @ prediction changes. For example, a loop-closing conditional jump is mispredicted 
//  @ once rather than twice.
//  @ The predictor table is indexed with the instruction address bits, so that the
//  @ processor can fetch a prediction for every instruction before the instruction is 
//  @ decoded. 
class BIMODAL : public BRANCHPREDICTOR 
{
private:
  enum 
  {
    TABLESIZEBIT = 13,
    TABLESIZE = 1<<TABLESIZEBIT,
  };

  static VOID Instruction(INS ins, VOID *v);
  static VOID CondBranch(BIMODAL *bm, ADDRINT ip, BOOL taken, THREADID tid);
  inline INT8 *HistAddress(ADDRINT ip, THREADID tid);

public:
  /* ===================================================================== */
  /* Bimodal predictor BPT                                                 */
  /* ===================================================================== */
  BimodalThreadInfo PredictorData[MAX_PREDICTOR_THREAD]; 

  /// @ Activate - register the instruction callback.
  VOID Activate()
  {
    INS_AddInstrumentFunction(Instruction, this);
  }
};

inline VOID BIMODAL::Instruction(INS ins, VOID *v)
{
  BIMODAL *bm = static_cast<BIMODAL*>(v);

  static INT32 NopCount = 0;
  bm->InitChannelAddr(INS_IsNop(ins), NopCount, INS_Address(ins));

  if (INS_IsBranchOrCall(ins) && INS_HasFallThrough(ins))
     {
     INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                              (AFUNPTR)CondBranch, 
                              IARG_PTR, (void *)bm, 
                              IARG_PTR, INS_Address(ins),
                              IARG_BRANCH_TAKEN, 
                              IARG_THREAD_ID,
                              IARG_END);
     }
}

inline INT8 * BIMODAL::HistAddress(ADDRINT ip, THREADID tid)
{
  ASSERTX(TABLESIZE % 2 == 0);

  INT8* InfoBranchHist = (INT8*) PredictorData[tid].InfoBranchHist;

  INT8 *Addr =  &(InfoBranchHist[ip & (TABLESIZE-1)]);

  ASSERTX(Addr >= &InfoBranchHist[0]);
  ASSERTX(Addr <= &InfoBranchHist[TABLESIZE-1]);
  return Addr;
}

inline VOID BIMODAL::CondBranch(BIMODAL *bm, ADDRINT ip, BOOL taken, THREADID tid)
{
  // Single thread profile enabled.
  if (bm->GetSingleOSThreadID() && PIN_GetTid()!=bm->GetSingleOSThreadID()) return; 

  // get the Info entry.
  BimodalThreadInfo *Info = &bm->PredictorData[tid];
  Info->Setup(TABLESIZE, sizeof(INT8));

  // record this prediction.
  Info->InfoReferences++;

  // 1, 0 represents weakly not-taken and strongly not-taken respectively.
  // 2, 3 represents weakly taken and strongly taken respectively.
  // 
  // take out the last 2 bits.
  INT8 *BranchHistEntry = bm->HistAddress(ip, tid);
  ASSERTX(*BranchHistEntry <= 3);
  INT8 History = *BranchHistEntry & 0x3;

  // this predictor would predict correctly if the history is on 
  // the weakly taken or strongly taken side.
  Info->InfoPredicts += (taken && History >= 2);
  // this predictor would predict correctly if the history is on 
  // the weakly not-taken or strongly not-taken side.
  Info->InfoPredicts += (!taken && History <= 1);

  // adjust the history.
  INT8 delta = 0;
  if (taken && History < 3)  delta =  1;
  if (!taken && History > 0) delta = -1;
  
  *BranchHistEntry += delta;
}

#endif // __BIMODAL_H
