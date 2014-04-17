/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2011 Intel Corporation. All rights reserved.
 
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
//
// @ORIGINAL_AUTHOR: Artur Klauser
//

/*! @file
 *  This file contains an ISA-portable PIN tool for functional simulation of
 *  instruction+data TLB+cache hieraries
 */

#include "pin.H"
#include "common.H"
#include "cache.H"

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,    "pintool",
    "o", "idcaches.out", "specify icache file name");
KNOB<UINT32> KnobLineSize(KNOB_MODE_WRITEONCE, "pintool",
    "b","64", "cache block size in bytes");
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
    "a","4", "cache associativity (1 for direct mapped)");
KNOB<string> KnobSetType(KNOB_MODE_WRITEONCE, "pintool",
    "s","LRU", "cache set type (RR for roundrobin)");
KNOB<UINT32> KnobCacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "c","32", "cache size in kilobytes");
KNOB<UINT32> KnobThread(KNOB_MODE_WRITEONCE, "pintool",
    "t","0", "thread ID to collect the simulation results on");
KNOB<UINT32> KnobWriteMissAllocate(KNOB_MODE_WRITEONCE, "pintool",
    "w","0", "write miss allocate (0 for allocate, 1 for not allocate ");
/* ===================================================================== */

// all caches and tlbs.
CACHE* il1 =  NULL;
CACHE* dl1 =  NULL; 

LOCALFUN VOID Fini(int code, VOID * v)
{
std::ofstream out(KnobOutputFile.Value().c_str());

out << "PIN:MEMLATENCIES 1.0. 0x0\n";
out << "#\n" << "# L1 ICACHE stats\n" << "#\n";
out << il1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_ICACHE);

out << "PIN:MEMLATENCIES 1.0. 0x0\n";
out << "#\n" << "# L1 DCACHE stats\n" << "#\n";
out << dl1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
}

LOCALFUN VOID InsRef(ADDRINT addr, THREADID tid)
{
// this is the thread we want to collect the profile on ?
if (KnobThread.Value() != tid) return;

const CACHE_BASE::ACCESS_TYPE accessType = CACHE_BASE::ACCESS_TYPE_LOAD;
// first level I-cache
il1->AccessSingleLine(addr, accessType);
}

LOCALFUN VOID MemRefMulti(ADDRINT addr, UINT32 size, 
                          CACHE_BASE::ACCESS_TYPE accessType,
                          THREADID tid)
{
// this is the thread we want to collect the profile on ?
if (KnobThread.Value() != tid) return;

// first level D-cache
dl1->Access(addr, size, accessType);
}

LOCALFUN VOID MemRefSingle(ADDRINT addr, UINT32 size, 
                           CACHE_BASE::ACCESS_TYPE accessType,
                           THREADID tid)
{
// this is the thread we want to collect the profile on ?
if (KnobThread.Value() != tid) return;

// first level D-cache
dl1->AccessSingleLine(addr, accessType);
}

LOCALFUN VOID Instruction(INS ins, VOID *v)
{
// all instruction fetches access I-cache
INS_InsertCall(
    ins, IPOINT_BEFORE, (AFUNPTR)InsRef,
    IARG_INST_PTR,
    IARG_THREAD_ID,
    IARG_END);

if (INS_IsMemoryRead(ins))
   {
   const UINT32 size = INS_MemoryReadSize(ins);
   const AFUNPTR countFun = (size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

   // only predicated-on memory instructions access D-cache
   INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, countFun,
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_LOAD,
            IARG_THREAD_ID,
            IARG_END);
    }

if (INS_IsMemoryWrite(ins))
   {
   const UINT32 size = INS_MemoryWriteSize(ins);
   const AFUNPTR countFun = (size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

   // only predicated-on memory instructions access D-cache
   INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, countFun,
            IARG_MEMORYWRITE_EA,
            IARG_MEMORYWRITE_SIZE,
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_STORE,
            IARG_THREAD_ID,
            IARG_END);
    }
}

GLOBALFUN int main(int argc, char *argv[])
{
PIN_Init(argc, argv);

// level 1 instruction cache.
il1 = new CACHE("L1 Inst Cache",
                KnobCacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

// level 1 data cache.
dl1 = new CACHE("L1 Data Cache",
                KnobCacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

ASSERTX(il1 && dl1);

INS_AddInstrumentFunction(Instruction, 0);
PIN_AddFiniFunction(Fini, 0);

// Never returns
PIN_StartProgram();
return 0; // make compiler happy
}
