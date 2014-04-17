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

#ifndef _BRANCHPREDICTOR_H
#define _BRANCHPREDICTOR_H

#include <iostream>   // For ofstream.
using namespace INSTLIB;  // For ICOUNT

#define COVERT_CHANNEL_START_ADDR 0x7F0B5A8E5000

#define ABSTRACT_CLASS =0
#define MAX_PREDICTOR_THREAD 256 

/* ===================================================================== */
/* generic per thread predictor stats                                    */
/* ===================================================================== */
class ThreadInfo
{
public:
  // thread initialization status.
  enum 
  {
    THREAD_INFO_UNINITIALIZED = 0,
    THREAD_INFO_INITIALIZED  
  };

  // this stats initialized ?
  UINT64   InfoInitialized;
  // number of times branch predicted using this predictor.
  UINT64   InfoReferences;
  // number of times branch predicted correctly using this predictor.
  UINT64   InfoPredicts;
  // branch history table.
  VOID*    InfoBranchHist;
  // the number of bytes the branch history table is.
  UINT64   InfoBranchHistSize;

  // interpreter specific branch predicts.
  UINT64   InfoInterpReferences;
  UINT64   InfoInterpPredicts;

  // the PIN-assigned ID of the thread that uses this stats.
  THREADID     InfoPINThreadID;
  // the OS-assigned ID of the thread that uses this stats.
  OS_THREAD_ID InfoOSThreadID;

  /// @ constructor.
  ThreadInfo() : InfoInitialized(THREAD_INFO_UNINITIALIZED), 
                 InfoReferences(0), InfoPredicts(0), 
                 InfoBranchHist(0), InfoBranchHistSize(0), 
                 InfoPINThreadID(0), InfoOSThreadID(0)
  {
    // Do nothing.
  }

  /// member functions ...

  /// @ Setup - Set up the predictor table for use.
  //  @
  //  @ paramaters.
  //  @ TableSize - Number of entries in the table.
  //  @ EntrySize - Size of each entry in the table.
  //  @
  //  @ side effects - The predictor table set up.
  //  @
  //  @ return - None.
  virtual void Setup(UINT64 TableSize, UINT64 EntrySize)
  {
    if (InfoInitialized) return;

    // initialize it.
    ASSERTX(!InfoBranchHist);
    InfoBranchHistSize = EntrySize * TableSize;
    InfoBranchHist =  malloc(InfoBranchHistSize);
    InfoInitialized = THREAD_INFO_INITIALIZED ;
    InfoPINThreadID = PIN_ThreadId();
    InfoOSThreadID = PIN_GetTid();   
    return;
  }

  /// @ Shutdown - Shutdown the predictor table in use.
  //  @
  //  @ paramaters - None.
  //  @
  //  @ side effects - The predictor table freed.
  //  @
  //  @ return - None.
  virtual void Shutdown() 
  {
    if (!InfoInitialized) return;
    
    // de-initialize it.
    ASSERTX(InfoBranchHist);
    delete (INT8 *) InfoBranchHist;
    InfoInitialized = THREAD_INFO_UNINITIALIZED;
    return;
  }

  /// @ DumpInfo - DumpInfo the statistics for the branch predictor.
  //  @
  //  @ paramaters.
  //  @ OutFile - The file to dump to.
  //  @ ICount - The number of instructions executed.
  //  @
  //  @ side effects - The statistics of the branch predictor
  //  @ dumped.
  //  @
  //  @ return - None.
  virtual void DumpInfo(ofstream * OutFile, ICOUNT *ICount)
  {
    *OutFile << "PIN Thread ID " << InfoPINThreadID << " OS Thread ID " 
             << InfoOSThreadID << dec << endl;
    *OutFile << "Instruction Executed = " << ICount->Count(InfoPINThreadID)
             << dec << endl;
    *OutFile << "Branch Predicted = " << InfoReferences
             << dec << endl;
    *OutFile << "Branch Predicted in Interpreter = " << InfoInterpReferences
             << dec << endl;
    *OutFile << "Branch MisPredicted = " << InfoReferences - InfoPredicts
             << dec << endl;
    *OutFile << "Branch MisPredicted in Interpreter = " << InfoInterpReferences - InfoInterpPredicts
             << dec << endl;
    *OutFile << "Misprediction Rate = " 
             << 100.0 * (double)(InfoReferences - InfoPredicts)/InfoReferences
             << dec << endl;
    *OutFile << "Predicted MPKI = "              
             << 1000.0 * (double)(InfoReferences - InfoPredicts)/ICount->Count(InfoPINThreadID)
             << dec << endl;
    return;
  }
};

/* ===================================================================== */
/* per thread predictor stats for the bimodal branch predictor           */
/* ===================================================================== */
class BimodalThreadInfo : public ThreadInfo
{
public:
  /// @ DumpInfo - DumpInfo the statistics of the bimodal branch predictor.
  virtual void DumpInfo(ofstream *OutFile, ICOUNT *ICount)
  {
    if (!InfoInitialized) return;

    *OutFile << "==== Bimodal Predictor ==============\n";
    ThreadInfo::DumpInfo(OutFile, ICount);
  }
};

/* ===================================================================== */
/* per thread predictor stats for the gshare branch predictor            */
/* ===================================================================== */
class GshareThreadInfo : public ThreadInfo
{
public:
  // the global history register
  UINT64 InfoGlobalHist;

  /// @ DumpInfo - DumpInfo the statistics of the gshare branch predictor.
  virtual void DumpInfo(ofstream *OutFile, ICOUNT *ICount)
  {
    if (!InfoInitialized) return;

    *OutFile << "==== Gshare Predictor ==============\n";
    ThreadInfo::DumpInfo(OutFile, ICount);
  }

};

/* ===================================================================== */
/* per thread predictor stats for the simple indirect branch predictor   */
/* ===================================================================== */
class SimpleIndirThreadInfo : public ThreadInfo
{
public:
  /// @ DumpInfo - DumpInfo the statistics of the simple indirect branch predictor.
  virtual void DumpInfo(ofstream *OutFile, ICOUNT *ICount)
  {
    if (!InfoInitialized) return;

    *OutFile << "==== Simple Indirect Predictor ==============\n";
    ThreadInfo::DumpInfo(OutFile, ICount);
  }
};

/* ===================================================================== */
/* per thread predictor stats for the vpc simple indirect branch predictor   */
/* ===================================================================== */
class VPCSimpleIndirThreadInfo : public ThreadInfo
{
public:
  /// @ DumpInfo - DumpInfo the statistics of the simple indirect branch predictor.
  virtual void DumpInfo(ofstream *OutFile, ICOUNT *ICount)
  {
    if (!InfoInitialized) return;

    *OutFile << "==== VPC Simple Indirect Predictor ==============\n";
    ThreadInfo::DumpInfo(OutFile, ICount);
  }
};



/* ===================================================================== */
/* per thread predictor stats for the target cache branch predictor      */
/* ===================================================================== */
class TargetCacheThreadInfo : public ThreadInfo
{
public:
  // path history table.
  UINT64 InfoGlobalHist;

  /// @ DumpInfo - DumpInfo the statistics of the target cache branch predictor.
  virtual void DumpInfo(ofstream *OutFile, ICOUNT *ICount)
  {
    if (!InfoInitialized) return;

    *OutFile << "==== Target Cache Predictor ==============\n";
    ThreadInfo::DumpInfo(OutFile, ICount);
  }
};

/// @ BRANCHPREDICTOR - this class implements a generic 
//  @ branch predictor. 
class BRANCHPREDICTOR 
{
private:
  // the only thread to profile.
  OS_THREAD_ID SingleOSThreadID;

  ADDRINT ChannelVPC;
  ADDRINT ChannelStartAddr;
  ADDRINT ChannelEndAddr;

public:
  /// @ constructor.
  BRANCHPREDICTOR() : SingleOSThreadID(0), ChannelStartAddr(0), ChannelEndAddr(0)
  {
    // Do nothing.
  }
  OS_THREAD_ID GetSingleOSThreadID() const
  {
    return SingleOSThreadID;
  }
  void SetSingleOSThreadID(OS_THREAD_ID _SingleOSThreadID)
  {
    SingleOSThreadID = _SingleOSThreadID;
  }

  void InitChannelAddr(bool IsNop, INT32 &NopCount, ADDRINT InsAddr)
  {
     // Try to detect 255 consecutive NOPs.
     if (IsNop) NopCount ++;
     if (NopCount == 255) 
     {
        ChannelVPC = InsAddr-255+1;
        ChannelStartAddr = *(UINT32*)(InsAddr+2);
        ChannelEndAddr =   *(UINT32*)(InsAddr+6);
        printf ("Established Channel 0x%x, Starting Address 0x%x, End Address 0x%x\n", 
                ChannelVPC, ChannelStartAddr, ChannelEndAddr);
        NopCount = 0;
     }
     else 
     {
       if (!IsNop) NopCount = 0;
     }
  }
 
  BOOL IsInterpBranch(ADDRINT ip)
  {
     if (ip > ChannelStartAddr && ip < ChannelEndAddr)
        return 1;
     return 0;
  }

  virtual VOID Activate() ABSTRACT_CLASS;

};

#endif // _BRANCHPREDICTOR_H
