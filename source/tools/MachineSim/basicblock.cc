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
/* This file contains an PIN tool for basicblock level instrumentation   */
/* ===================================================================== */

#include "pin.H"
#include "utils.hh"

/* DoBasicBlockCount - count # of basicblocks executed */
LOCALFUN void DoBasicBlockCount(void) 
{
    SimTheOne->IncBasicBlock();
}

LOCALFUN VOID SimpleBasicBlockCount(TRACE trace, VOID *v)
{
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to docount before every bbl, passing the number of instructions
        BBL_InsertCall(bbl, IPOINT_BEFORE, DoBasicBlockCount, IARG_END);
    }
}
 

/* TraceInstrument - setup call to do basicblock level instrumentation */
VOID TraceInstrument(TRACE trace, VOID *v)
{
    if (SimOpts->get_ins_count()) SimpleBasicBlockCount(trace, v);
    return;
}

void MachineSimBasicBlockModuleInit(void)
{
    /* do nothing right now */
}

void MachineSimBasicBlockModuleFini(void)
{
    /* do nothing right now */
}

