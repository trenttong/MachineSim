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

#ifndef _GSHARE_H
#define _GSHARE_H

#include "branch_predictor.H"

/// @ GSHARE - this class implements a GSHARE branch predictor. 
//  @ Gshare is a global prediction scheme. The global share 
//  @(gshare) algorithm uses two levels of branch-history 
//  @ information to dynamically predict the direction of branches. 
//  @ The first level registers the history of the last k branches 
//  @ faced. This represents the global branching behavior. 
//  @ This level can be implemented by providing a global 
//  @ branch history register. This is basically a shift register
//  @ that enters a 1 for every taken branch and a 0 for every 
//  @ untaken branch. The second level of branch 
//  @ history information registers the branching of the last s 
//  @ occurrences of the specific pattern of the k branches. This 
//  @ information is kept in the branch prediction table. The 
//  @ gshare algorithm works by taking the lower bits of the 
//  @ branch target address and exclusive oring them with the 
//  @ history register to get the index that should be used with 
//  @ the prediction table.
class GSHARE : public BRANCHPREDICTOR 
{
private:
  enum 
  {
    TABLESIZEBIT = 13,
    TABLESIZE = 1<<TABLESIZEBIT,
  };

  static VOID Instruction(INS ins, VOID *v);
  static VOID CondBranch(GSHARE *gs, ADDRINT ip, BOOL taken, THREADID tid);
  inline INT8 *HistAddress(ADDRINT ip, THREADID tid);

public:
  /* ===================================================================== */
  /* Gshare predictor BPT                                                  */
  /* ===================================================================== */
  GshareThreadInfo PredictorData[MAX_PREDICTOR_THREAD]; 

  /// @ Activate - register the instruction callback.
  VOID Activate()
  {
    INS_AddInstrumentFunction(Instruction, this);
  }
};

inline VOID GSHARE::Instruction(INS ins, VOID *v)
{
  GSHARE *gs = static_cast<GSHARE*>(v);
  if (INS_IsBranchOrCall(ins) && INS_HasFallThrough(ins))
     {
     INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                              (AFUNPTR)CondBranch, 
                              IARG_PTR, (void *)gs,  
                              IARG_INST_PTR,  
                              IARG_BRANCH_TAKEN, 
                              IARG_THREAD_ID,
                              IARG_END);
     }
}

inline INT8 * GSHARE::HistAddress(ADDRINT ip, THREADID tid)
{
  ASSERTX(TABLESIZE % 2 == 0);

  INT8* InfoBranchHist = (INT8*) PredictorData[tid].InfoBranchHist;
  UINT64 InfoGlobalHist = PredictorData[tid].InfoGlobalHist;

  INT8* addr = &(InfoBranchHist[(ip & (TABLESIZE -1)) ^ InfoGlobalHist]);

  // check whether access out of bound.
  ASSERTX(addr >= &InfoBranchHist[0]);
  ASSERTX(addr <= &InfoBranchHist[TABLESIZE-1]);
  return addr;
}

inline VOID GSHARE::CondBranch(GSHARE *gs, ADDRINT ip, BOOL taken, THREADID tid)
{
  // Single thread profile enabled.
  if (gs->GetSingleOSThreadID() && PIN_GetTid()!=gs->GetSingleOSThreadID()) return; 

  // get the Info entry.
  GshareThreadInfo *Info = &gs->PredictorData[tid];
  Info->Setup(TABLESIZE, sizeof(INT8));

  // record this prediction.
  Info->InfoReferences++;

  // 1, 0 represents weakly not taken and strongly not taken respectively.
  // 2, 3 represents weakly taken and strongly taken respectively.
  // 
  // take out the last 2 bits.
  INT8 *BranchHistEntry = gs->HistAddress(ip, tid);
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

  // update the global history register. shift in the direction of the last branch.
  Info->InfoGlobalHist <<= 1;
  Info->InfoGlobalHist |= taken;
  Info->InfoGlobalHist &= (TABLESIZE - 1);
}

#endif // _GSHARE_H
