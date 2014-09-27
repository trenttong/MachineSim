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
/* This file contains an PIN tool for functional simulation of           */
/* instruction+data TLB+cache hieraries                                  */
/* ===================================================================== */

#include "pin.H"
#include "utils.hh"
#include <iostream>

/* ===================================================================== */
/* Globals variables */
/* ===================================================================== */
SIMXLATOR * SimXlator  = NULL;
SIMPARAMS * SimWait    = NULL;
SIMOPTS   * SimOpts    = NULL;
SIMSTATS  * SimStats   = NULL;
SIMGLOBALS* SimTheOne  = NULL;


/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */
KNOB<BOOL>   KnobInstructionCountOnly(KNOB_MODE_WRITEONCE     , "pintool",  "instcount"     ,"0"    , "instruction count only");
KNOB<BOOL>   KnobMemorySimulationOnly(KNOB_MODE_WRITEONCE     , "pintool",  "memsim"        ,"0"    , "simulate memory only");
KNOB<BOOL>   KnobEnableTraceRecord(KNOB_MODE_WRITEONCE        , "pintool",  "tc"            ,"0"    , "Enable Trace simulation");
KNOB<UINT32> KnobWthdCount(KNOB_MODE_WRITEONCE                , "pintool",  "wthd"          ,"0"    , "Number of worker threads created before simulation");
KNOB<UINT32> KnobWriteMissAllocate(KNOB_MODE_WRITEONCE        , "pintool",  "w"             ,"0"    , "write miss allocate (0 for allocate, 1 for not allocate ");
KNOB<UINT32> KnobCacheDetailPrint(KNOB_MODE_WRITEONCE         , "pintool",  "dp"            ,"0"    , "Enable detailed private cache miss data");
KNOB<string> KnobSetType(KNOB_MODE_WRITEONCE                  , "pintool",  "r"             ,"LRU"  , "cache replacement policy");
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE               , "pintool",  "o"             ,"cout" , "specify icache file name");
KNOB<UINT64> KnobMaxSimInstCount(KNOB_MODE_WRITEONCE          , "pintool",  "insc"          ,"5000000000", "Number of cores in the simulated system");
KNOB<string> KnobConfigFile(KNOB_MODE_WRITEONCE               , "pintool",  "c"             ,"/home/xtong/config.xml" , "specify simulation configuration file name");


/* ===================================================================== */
/* Initialization and Finalization */
/* ===================================================================== */
/// initialize_simobjs - initialize all the global objects of the simulator.
LOCALFUN VOID initialize_simobjs()
{
    SimStats   = SIMSTATS::get_singleton();
    SimXlator  = SIMXLATOR::get_singleton();
    SimWait    = SIMPARAMS::get_singleton();
    SimOpts    = SIMOPTS::get_singleton();
    SimTheOne  = SIMGLOBALS::get_singleton();
}

/// finalize_simobjs - destroy all the global objects of the simulator.
LOCALFUN VOID finalize_simobjs()
{
   delete SimOpts    ;  SimOpts   = NULL;
   delete SimXlator  ;  SimXlator = NULL;
   delete SimWait    ;  SimWait   = NULL;
   delete SimStats   ;  SimStats  = NULL;
   delete SimTheOne  ;  SimTheOne = NULL;
}

/// initialize_SimOpts - initialize simulation options.
LOCALFUN VOID initialize_SimOpts()
{
    SimOpts->set_ins_count(KnobInstructionCountOnly.Value());
    SimOpts->set_mem_simul(KnobMemorySimulationOnly.Value());
    SimOpts->set_replacepolicy(KnobSetType.Value());
    SimOpts->set_tracerecord(KnobEnableTraceRecord.Value());
    SimOpts->set_maxsiminst(KnobMaxSimInstCount.Value());
    SimOpts->set_xml_parser(new ParseXML());
    SimOpts->get_xml_parser()->parse(KnobConfigFile.Value().c_str());
}

LOCALFUN VOID initialize_SimTheOne() 
{
    clock_gettime(CLOCK_MONOTONIC, SimTheOne->get_time_init());
}

/// finalize_SimOpts - finalize the simulation options.
LOCALFUN VOID finalize_SimOpts()
{
    SimOpts->reset();
}

/// initialize_SimWait - initialize simulation wait reasons.
LOCALFUN VOID initialize_SimWait()
{
///    SimWait->setwait(SIMPARAMS::WAIT_WORKER_THREAD);
}

/// main_module_init - initialize the main module of the simulator.
VOID main_module_init()
{
    initialize_simobjs();
    initialize_SimOpts();
    initialize_SimWait();
    initialize_SimTheOne();
}

/// main_module_fini - finalize the main module of the simulator.
VOID main_module_fini() 
{
   finalize_simobjs();
   finalize_SimOpts();
}

LOCALFUN VOID Fini(int code, VOID * v)
{
   /* finalize modules. */
   cache_and_tlb_module_fini();
   instruction_module_fini();
   basicblock_module_fini();
   main_module_fini();
}

LOCALFUN INT32 Usage()
{
    LOG("-instcount\t\t\t Turn on instruction count\n");
    LOG("-memsim\t\t\t Turn on cache hiearchy simulation\n");
    LOG("This pin tool implements multiple levels of caches and TLBs.\n\n");
    return -1;
}

/* ===================================================================== */
/* The fun begins ... */
/* ===================================================================== */
GLOBALFUN int main(int argc, char *argv[])
{
    /// initialize symbols for image instrumentation.
    PIN_InitSymbols();

    if (PIN_Init(argc,argv)) return Usage();

    /// initialize the main module.
    main_module_init();

    /// initialize instruction module.
    instruction_module_init();

    /// initialize basicblock module.
    basicblock_module_init();

    /// initialize the cache module simulation.
    cache_and_tlb_module_init();

    RTN_AddInstrumentFunction(RoutineInstrument, 0);
    INS_AddInstrumentFunction(InstructionInstrument, 0);
    TRACE_AddInstrumentFunction(TraceInstrument, 0);
    /// IMG_AddInstrumentFunction(ImageInstrument, 0);

    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    return 0; // make compiler happy
}
