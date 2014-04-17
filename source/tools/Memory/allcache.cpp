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
    "o", "caches.out", "specify icache file name");
KNOB<UINT32> KnobLineSize(KNOB_MODE_WRITEONCE, "pintool",
    "b","64", "cache block size in bytes");
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
    "a","4", "cache associativity (1 for direct mapped)");
KNOB<string> KnobSetType(KNOB_MODE_WRITEONCE, "pintool",
    "s","LRU", "cache set type (RR for roundrobin)");
KNOB<UINT32> KnobWriteMissAllocate(KNOB_MODE_WRITEONCE, "pintool",
    "w","0", "write miss allocate (0 for allocate, 1 for not allocate ");
KNOB<UINT32> KnobDummyCacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "ldc","32", "L1 cache size in kilobytes");
KNOB<UINT32> KnobL1CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "l1c","32", "L1 cache size in kilobytes");
KNOB<UINT32> KnobL2CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "l2c","2048", "L2 unified cache size in kilobytes");
KNOB<UINT32> KnobL3CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "l3c","16384", "L3 unified cache size in kilobytes");
/* ===================================================================== */


// all caches and tlbs.
CACHE* itlb = NULL;
CACHE* dtlb = NULL;
CACHE* il1 =  NULL;
CACHE* dl1 =  NULL; 
CACHE* ul2 =  NULL;
CACHE* ul3 =  NULL;

LOCALFUN VOID Fini(int code, VOID * v)
{
std::ofstream out(KnobOutputFile.Value().c_str());

out << "PIN:MEMLATENCIES 1.0. 0x0\n";
out << "#\n" << "# L1 ICACHE stats\n" << "#\n";
out << il1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_ICACHE);

out << "PIN:MEMLATENCIES 1.0. 0x0\n";
out << "#\n" << "# L1 DCACHE stats\n" << "#\n";
out << dl1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE);

out << "PIN:MEMLATENCIES 1.0. 0x0\n";
out << "#\n" << "# L2 unified CACHE stats\n" << "#\n";
out << ul2->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE);

out << "PIN:MEMLATENCIES 1.0. 0x0\n";
out << "#\n" << "# L3 unified CACHE stats\n" << "#\n";
out << ul3->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
}

LOCALFUN VOID Ul2Access(ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE accessType)
{
// second level unified cache
const BOOL ul2Hit = ul2->Access(addr, size, accessType);

// third level unified cache
if ( ! ul2Hit) ul3->Access(addr, size, accessType);
}

LOCALFUN VOID InsRef(ADDRINT addr)
{
const UINT32 size = 1; // assuming access does not cross cache lines
const CACHE_BASE::ACCESS_TYPE accessType = CACHE_BASE::ACCESS_TYPE_LOAD;

// ITLB
itlb->AccessSingleLine(addr, accessType);

// first level I-cache
const BOOL il1Hit = il1->AccessSingleLine(addr, accessType);

// second level unified Cache
if ( ! il1Hit) Ul2Access(addr, size, accessType);
}

LOCALFUN VOID MemRefMulti(ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE accessType)
{
// DTLB
dtlb->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);

// first level D-cache
const BOOL dl1Hit = dl1->Access(addr, size, accessType);

// second level unified Cache
if ( ! dl1Hit) Ul2Access(addr, size, accessType);
}

LOCALFUN VOID MemRefSingle(ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE accessType)
{
// DTLB
dtlb->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);

// first level D-cache
const BOOL dl1Hit = dl1->AccessSingleLine(addr, accessType);

// second level unified Cache
if ( ! dl1Hit) Ul2Access(addr, size, accessType);
}

LOCALFUN VOID Instruction(INS ins, VOID *v)
{
// all instruction fetches access I-cache
INS_InsertCall(
    ins, IPOINT_BEFORE, (AFUNPTR)InsRef,
    IARG_INST_PTR,
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
            IARG_END);
    }
}

GLOBALFUN int main(int argc, char *argv[])
{
PIN_Init(argc, argv);

// level 1 instruction cache.
il1 = new CACHE("L1 Inst Cache",
                KnobL1CacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

// level 1 data cache.
dl1 = new CACHE("L1 Data Cache",
                KnobL1CacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

// level 2 unified cache
ul2 = new CACHE("L2 Unified Cache",
                KnobL2CacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

// level 3 unified cache
ul3 = new CACHE("L3 Unified Cache",
                KnobL3CacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

// instruction tlb
itlb = new CACHE("Instructin TLB Cache",
                KnobDummyCacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

// data tlb
dtlb = new CACHE("Data TLB Cache",
                KnobDummyCacheSize.Value() * KILO,
                KnobLineSize.Value(),
                KnobAssociativity.Value(),
                KnobSetType.Value(),
                KnobWriteMissAllocate.Value());

ASSERTX(il1 && dl1 && ul2 && ul3 && itlb && dtlb);

INS_AddInstrumentFunction(Instruction, 0);
PIN_AddFiniFunction(Fini, 0);

// Never returns
PIN_StartProgram();
return 0; // make compiler happy
}
