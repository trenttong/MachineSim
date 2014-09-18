/*BEGIN_LEGAL
Intel Open Source License

Copyright (c) 2002-2011 Intel CorpORAtion. All rights reserved.

Written by Xin Tong, University of Toronto.

Redistribution and use in source and binary fORMs, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary fORM must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel CorpORAtion nor the names of its contributors may be used to
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

/* ===================================================================== */
/* This file contains an PIN tool for instruction level instrumentation  */
/* ===================================================================== */

#include "pin.H"
#include "utils.hh"

/* ===================================================================== */
/* Cache Simulation Functions */
/* ===================================================================== */
VOID InsFetchRef(ADDRINT addr, THREADID tid);
VOID DataFetchRef(ADDRINT iaddr, ADDRINT addr, UINT32 size, UINT64 base, UINT64 idx, THREADID tid);
VOID DataWriteRef(ADDRINT iaddr, ADDRINT addr, UINT32 size, UINT64 base, UINT64 idx, THREADID tid);

/* ===================================================================== */
/* Globals variables */
/* ===================================================================== */
static FILE * tracefile = NULL;
static SimInsCount *siminscount = NULL;

/* ===================================================================== */
/* Parse static mapping every X billion instructions  this constructs    */
/* and print the memory layout of the program                            */
/* ===================================================================== */
LOCALFUN VOID ParseStaticAddrSpaceMap()
{
    UINT64 start, end, perm;
    UINT32 id = PIN_GetPid();
    // Open and read the /proc/id/maps file
    char name[128];
    sprintf(name, "/proc/%d/maps", id); 
    AddrSpaceMapParser *mp = new AddrSpaceMapParser(name);
    AddrSpaceMap       *rn = new AddrSpaceMap;
    while (mp->GetNextRegion(start, end, perm)) rn->RegisterRegion(start, end, perm);
    rn->PrettyPrint();
    delete mp;
    delete rn;
    return;
}

/* =============================================== */
/* PIN instrumentation Reference Count Functions.  */
/* =============================================== */
// This function is called before every instruction is executed
LOCALFUN VOID DoSimpleICount(ADDRINT ip, THREADID id) 
{ 
    if (!simwait->dosim()) return;

    // one more instructions executed.
    //UINT64 tcount = simaops->atom_uint64_inc((UINT64*)&GlobalInstCount);
    UINT64 tcount = simglobals->add_global_icount();
    if (!(tcount % (MEGA))) 
    {
      /// calculate elapsed time.
      clock_gettime(CLOCK_MONOTONIC, simglobals->get_time_fini());
      double seconds = ((double)simglobals->get_time_fini()->tv_sec + 
                       1.0e-9*simglobals->get_time_fini()->tv_nsec) -
                       ((double)simglobals->get_time_init()->tv_sec + 
                       1.0e-9*simglobals->get_time_fini()->tv_nsec);

      printf("Simulated %lluM instructions %lf MIPS\n", (unsigned long long) tcount/MEGA, (double) tcount/MEGA/seconds);

      /// printf("simopts->get_maxsiminst() is %d and GlobalInstCount is %d\n", 
      ///         simopts->get_maxsiminst(), GlobalInstCount);
      if (simglobals->get_global_icount() > simopts->get_maxsiminst())
      {
          printf("PIN_ExitApplication due to reaching maximum simulation threshold\n");
          PIN_ExitApplication(0);
      }

      if (simopts->get_static_addrspace_map()) ParseStaticAddrSpaceMap();
    }
}

    
/// @ SimpleInstructionCount - count the # of instructions executed.
VOID SimpleInstructionCount(INS ins, VOID *v)
{
    /// =========================================================
    // Insert a call to DoSimpleICount before every instruction.
    /// =========================================================
    INS_InsertCall(ins, IPOINT_BEFORE, 
                  (AFUNPTR)DoSimpleICount, 
                   IARG_INST_PTR, 
                   IARG_THREAD_ID, 
                   IARG_END);
}

/* ===================================================================== */
/* Printing Routines */
/* ===================================================================== */
LOCALFUN VOID instruction_module_print()
{
    char name[128];
    sprintf(name, "%s.%d", "instruction.out", PIN_GetPid());
    std::ofstream out(name);

    out << "#==================\n" << "# General stats\n" << "#====================\n";
    if (siminscount) out << siminscount->StatsLongAll();

    fprintf(stdout, "instruction stats dumped into %s.%d\n", "instruction.out", PIN_GetPid());
}

LOCALFUN VOID DoICountOnType(THREADID id, int type)
{
    if (type == SimInsCount::INSTYPE::INS_LOAD)   siminscount->IncLoad();
    if (type == SimInsCount::INSTYPE::INS_STORE)  siminscount->IncStore();
    if (type == SimInsCount::INSTYPE::INS_BRANCH) siminscount->IncBranch();
    if (type == SimInsCount::INSTYPE::INS_CALL)   siminscount->IncCall();
    if (type == SimInsCount::INSTYPE::INS_RET)    siminscount->IncRet();
}

/// @ InstructionCountOnType - count instruction based on type.
VOID InstructionCountOnType(INS ins, VOID *v)
{
    /// --------------------------------------------- ///
    //  Read or write memory location                  //
    /// --------------------------------------------- ///
    if (INS_IsMemoryRead(ins))
    {
       INS_InsertCall(ins, IPOINT_BEFORE, 
                     (AFUNPTR)DoICountOnType, 
                     IARG_THREAD_ID, 
                     IARG_UINT32, SimInsCount::INSTYPE::INS_LOAD,
                     IARG_END);
    }

    if (INS_IsMemoryWrite(ins))
    {
       INS_InsertCall(ins, IPOINT_BEFORE, 
                     (AFUNPTR)DoICountOnType, 
                     IARG_THREAD_ID, 
                     IARG_UINT32, SimInsCount::INSTYPE::INS_STORE,
                     IARG_END);
    }


    /// --------------------------------------------- ///
    //  Branch Instruction                             //
    /// --------------------------------------------- ///
    if (INS_IsBranch(ins))
    {
       INS_InsertCall(ins, IPOINT_BEFORE, 
                     (AFUNPTR)DoICountOnType, 
                     IARG_THREAD_ID, 
                     IARG_UINT32, SimInsCount::INSTYPE::INS_BRANCH,
                     IARG_END);
    }

    if (INS_IsCall(ins))
    {
       INS_InsertCall(ins, IPOINT_BEFORE, 
                     (AFUNPTR)DoICountOnType, 
                     IARG_THREAD_ID, 
                     IARG_UINT32, SimInsCount::INSTYPE::INS_CALL,
                     IARG_END);
    }

    if (INS_IsRet(ins))
    {
       INS_InsertCall(ins, IPOINT_BEFORE, 
                     (AFUNPTR)DoICountOnType, 
                     IARG_THREAD_ID, 
                     IARG_UINT32, SimInsCount::INSTYPE::INS_RET,
                     IARG_END);
    }
    return;
}

VOID CacheSim(INS ins,VOID *v)
{
    /// =========================================================
    //  Cache Simulation Callbacks.
    /// =========================================================

    /// --------------------------------------------- ///
    //  instruction cache simulation                   //
    /// --------------------------------------------- ///
    // all instruction fetches access I-cache
    // assume instruction access does not cross cache line.
    INS_InsertCall(ins, IPOINT_BEFORE,
                  (AFUNPTR)InsFetchRef,
                   IARG_INST_PTR,
                   IARG_THREAD_ID,
                   IARG_END);

    /// --------------------------------------------- ///
    //  data cache simulation                          //
    /// --------------------------------------------- ///
    if (INS_IsMemoryRead(ins))
    {
            /// --------------------------------------------- ///
            //  Read an data memory location                   //
            /// --------------------------------------------- ///

            if (REG_valid(INS_MemoryBaseReg(ins)) && REG_valid(INS_MemoryIndexReg(ins)))
            {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                                     (AFUNPTR)DataFetchRef,
                                     IARG_INST_PTR,
                                     IARG_MEMORYREAD_EA,
                                     IARG_MEMORYREAD_SIZE,
                                     IARG_REG_VALUE, INS_MemoryBaseReg(ins),
                                     IARG_REG_VALUE, INS_MemoryIndexReg(ins),
                                     IARG_THREAD_ID,
                                     IARG_END);
            }
            else if (REG_valid(INS_MemoryBaseReg(ins)))
            {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                                     (AFUNPTR)DataFetchRef,
                                     IARG_INST_PTR,
                                     IARG_MEMORYREAD_EA,
                                     IARG_MEMORYREAD_SIZE,
                                     IARG_REG_VALUE, INS_MemoryBaseReg(ins),
                                     IARG_UINT32, 0,
                                     IARG_THREAD_ID,
                                     IARG_END);
            }
            else 
            {
            // only predicated-on memory instructions access D-cache
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                                     (AFUNPTR)DataFetchRef,
                                     IARG_INST_PTR,
                                     IARG_MEMORYREAD_EA,
                                     IARG_MEMORYREAD_SIZE,
                                     IARG_UINT32, 0,
                                     IARG_UINT32, 0,
                                     IARG_THREAD_ID,
                                     IARG_END);
            }
     }

     if (INS_IsMemoryWrite(ins))
     {

            /// --------------------------------------------- ///
            //  Write an data memory location                  //
            /// --------------------------------------------- ///

            if (REG_valid(INS_MemoryBaseReg(ins)) && REG_valid(INS_MemoryIndexReg(ins)))
            {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                                     (AFUNPTR)DataWriteRef,
                                     IARG_INST_PTR,
                                     IARG_MEMORYWRITE_EA,
                                     IARG_MEMORYWRITE_SIZE,
                                     IARG_REG_VALUE, INS_MemoryBaseReg(ins),
                                     IARG_REG_VALUE, INS_MemoryIndexReg(ins),
                                     IARG_THREAD_ID,
                                     IARG_END);
            } 
            else if (REG_valid(INS_MemoryBaseReg(ins)))
            {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                                     (AFUNPTR)DataWriteRef,
                                     IARG_INST_PTR,
                                     IARG_MEMORYWRITE_EA,
                                     IARG_MEMORYWRITE_SIZE,
                                     IARG_REG_VALUE, INS_MemoryBaseReg(ins),
                                     IARG_UINT32, 0,
                                     IARG_THREAD_ID,
                                     IARG_END);
            }
            else 
            {
            // only predicated-on memory instructions access D-cache
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                                     (AFUNPTR)DataWriteRef,
                                     IARG_INST_PTR,
                                     IARG_MEMORYWRITE_EA,
                                     IARG_MEMORYWRITE_SIZE,
                                     IARG_UINT32, 0,
                                     IARG_UINT32, 0,
                                     IARG_THREAD_ID,
                                     IARG_END);
            }
     }
}

/* ===================================================================== */
/* Instruction Printing for TraceBased Simulation */
/* ===================================================================== */
VOID DumpPC(VOID *ip, USIZE size, UINT32 memOperands)
{
    fprintf(tracefile, "%p  %d  %d  ", ip, size, memOperands);
}

VOID RecordMemRead(VOID * ip, VOID * addr, USIZE size)
{
    fprintf(tracefile,"0x00 %p %d\n", addr,size);
}

VOID RecordMemWrite(VOID * ip, VOID * addr, USIZE size)
{
    fprintf(tracefile,"0x01 %p %d\n", addr, size);
}

VOID DumpInstructionBytes(VOID *ip, USIZE size)
{
    for (UINT8 i=0; i<size; i++) fprintf(tracefile, "%x ",(*((UINT8*)ip+i)) );
    fprintf(tracefile,"\n");
}

// Pin calls this function every time a new instruction is encountered
VOID TraceSim(INS ins, VOID *v)
{
    USIZE size = INS_Size(ins);

    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Insert a call to DumpPC before every instruction, and pass it the IP
    INS_InsertCall(ins, IPOINT_BEFORE,
                   (AFUNPTR)DumpPC,
                   IARG_INST_PTR,
                   IARG_UINT32 , size ,
                   IARG_UINT32, memOperands,
                   IARG_END);

    INS_InsertCall(ins, IPOINT_BEFORE,
                   (AFUNPTR)DumpInstructionBytes,
                   IARG_INST_PTR,
                   IARG_UINT32 , size ,
                   IARG_END);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertCall(ins, IPOINT_BEFORE,
                           (AFUNPTR)RecordMemRead,
                           IARG_INST_PTR,
                           IARG_MEMORYOP_EA, memOp,
                           IARG_MEMORYREAD_SIZE,
                           IARG_END);
        }

        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertCall(ins, IPOINT_BEFORE,
                           (AFUNPTR)RecordMemWrite,
                           IARG_INST_PTR,
                           IARG_MEMORYOP_EA, memOp,
                           IARG_MEMORYWRITE_SIZE,
                           IARG_END);
        }
    }
}

/// instrument every instruction.
VOID InstructionInstrument(INS ins, VOID *v)
{
    // Simple instruction count.
    SimpleInstructionCount(ins, v);
    // Cache simulation enabled.
    CacheSim(ins, v);

    // Trace base simulation enabled.
    if (simopts->get_tracerecord()) TraceSim(ins, v);
}

void instruction_module_init(void)
{
    siminscount = new SimInsCount();
}

void instruction_module_fini(void)
{
    instruction_module_print();
    if (siminscount) delete siminscount;
}
