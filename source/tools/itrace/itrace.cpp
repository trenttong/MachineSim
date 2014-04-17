/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2010 Intel Corporation. All rights reserved.
 
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
#include <stdio.h>
#include "pin.H"
#include <stdint.h>

#include <iostream>
#include <fstream>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "portability.H"

FILE * trace;
uint64_t trace_ins_cnt=0,ins_count=0;
char *pin_itrace_out;
unsigned int subtrace_cnt=0;

uint64_t start=10000LLU;
bool log_trace=false;

using namespace std;

#define MAX_INSTRUCTIONS  100000000 // 100M
// This function is called before every instruction is executed
// and prints the IP
VOID printip(VOID *ip, USIZE size, UINT32 memOperands)
{
	if(trace_ins_cnt > MAX_INSTRUCTIONS)
	{
		fprintf(stdout,"\nTrace_ins_cnt = %llu\n",trace_ins_cnt-1);
		exit(1);
		return;
	}
	if(ins_count > start)
	{
		if(ins_count==start+1)
			fprintf(stdout,"\nStarted logging\n");
		log_trace=true;
	}
        if(log_trace==true)
	{
		//fprintf(stdout,"\nStill logging %lld\n",trace_ins_cnt);
		trace_ins_cnt++;
		int ch;
		ch=fprintf(trace, "%p  %d  %d  ", ip, size, memOperands );
		if(ch<0)
		{
			fclose(trace);

			char filename[128];
			sprintf(filename,"%s_%u.out",pin_itrace_out,subtrace_cnt);
			trace = fopen(filename,"w");

			if(!trace)
			{
				fprintf(stdout,"ERROR: Cannot open PIN Itrace output file\n");
				exit(1);
			}

 			ch=fprintf(trace, "%p  %d  %d  ", ip, size, memOperands );
			if(ch<0)
			{ 
				fprintf(stdout,"ERROR: Cannot write to file anymore\n");
				exit(1);
			}			
		}
			//fprintf(stdout,"\ncannot write to file anymore\n");
	}
	ins_count++;
}


VOID dumpInstruction64Binary(VOID *ip, USIZE size)
{
	if(trace_ins_cnt >MAX_INSTRUCTIONS )
		return;

        if(log_trace==true)
	{
		unsigned char i = 0;
		//USIZE size = (unsigned char*)next_ip-(unsigned char*)ip;
		for (i=0; i<size; i++)
		{
			fprintf(trace, "%x ",(*((unsigned char *)ip+i)) );
		}
		fprintf(trace,"\n");
	}
}

// Print a memory read record
VOID RecordMemRead(VOID * ip, VOID * addr, USIZE size)
{
    if(trace_ins_cnt > MAX_INSTRUCTIONS)
		return;
    
    if(log_trace==true)
    	fprintf(trace,"0x00 %p %d\n", addr,size);
}

// Print a memory write record
VOID RecordMemWrite(VOID * ip, VOID * addr, USIZE size)
{
    if(trace_ins_cnt > MAX_INSTRUCTIONS)
		return;
    if(log_trace==true)
    	fprintf(trace,"0x01 %p %d\n", addr, size);
}



// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{

    USIZE size = INS_Size(ins);

    UINT32 memOperands = INS_MemoryOperandCount(ins);
    // Insert a call to printip before every instruction, and pass it the IP
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printip, IARG_INST_PTR, IARG_UINT32 , size , IARG_UINT32, memOperands, IARG_END);

    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)dumpInstruction64Binary, IARG_INST_PTR, IARG_UINT32 , size , IARG_END);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
    	if (INS_MemoryOperandIsRead(ins, memOp))
        {
	//	UINT32 refSize = INS_MemoryReadSize(ins);
          	  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE,IARG_END);
          	 // INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYOP_EA, memOp, refSize ,IARG_END);
	}	
        //if(INS_HasMemoryRead2(ins) )
	//{
        //  	  INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYREAD2_EA, IARG_MEMORYREAD_SIZE,IARG_END);
	//}	 
        
        // Note that in some architectures a single memory operand can be 
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
	//	UINT32 refSize = INS_MemoryWriteSize(ins);
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,IARG_INST_PTR,IARG_MEMORYOP_EA, memOp, IARG_MEMORYWRITE_SIZE,IARG_END);
            //INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,IARG_INST_PTR,IARG_MEMORYOP_EA, memOp, refSize,IARG_END);
        }
    }

}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    fclose(trace);
    fprintf(stdout,"\nend of program");
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("This Pintool prints the IPs of every instruction executed\n" 
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    pin_itrace_out = getenv ("PIN_ITRACE_OUT");

    char filename[128];

    if (pin_itrace_out == NULL)
      pin_itrace_out = "itrace";

    sprintf(filename,"%s_%u.out",pin_itrace_out,subtrace_cnt);
    trace = fopen(filename,"w");

    if(!trace)
    {
      fprintf(stdout,"ERROR: Cannot open PIN Itrace output file\n");
      exit(1);
    }

    printf("\n%s\n",filename);

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}
