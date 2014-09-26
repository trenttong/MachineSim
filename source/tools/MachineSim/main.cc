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
SIMXLATOR * simxlator  = NULL;
SIMPARAMS * simwait    = NULL;
SIMOPTS   * simopts    = NULL;
SIMSTATS  * simstats   = NULL;
SIMGLOBALS* simglobals = NULL;


/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */
KNOB<BOOL>   KnobEnableAddressShareDet(KNOB_MODE_WRITEONCE    , "pintool",  "dsd"  ,"0"    , "disable address sharing detection");
KNOB<BOOL>   KnobEnableTLBCoherence(KNOB_MODE_WRITEONCE       , "pintool",  "tlbc" ,"1"    , "enable tlb coherence");
KNOB<BOOL>   KnobEnablePTWalkTrace(KNOB_MODE_WRITEONCE        , "pintool",  "ptw"  ,"0"    , "enable page table walk trace analysis");
KNOB<BOOL>   KnobEnableDynAddrSpaceMap(KNOB_MODE_WRITEONCE    , "pintool",  "dasm"  ,"0"   , "enable dynamic address space map");
KNOB<BOOL>   KnobEnableStaticAddrSpaceMap(KNOB_MODE_WRITEONCE , "pintool",  "sasm"  ,"0"   , "enable static address space map");
KNOB<BOOL>   KnobEnableTraceRecord(KNOB_MODE_WRITEONCE        , "pintool",  "tc"   ,"0"    , "Enable Trace simulation");
KNOB<BOOL>   KnobEnableInsCount(KNOB_MODE_WRITEONCE           , "pintool",  "ic"   ,"0"    , "Enable Instruction Count");
KNOB<UINT32> KnobWthdCount(KNOB_MODE_WRITEONCE                , "pintool",  "wthd" ,"0"    , "Number of worker threads created before simulation");
KNOB<UINT32> KnobWriteMissAllocate(KNOB_MODE_WRITEONCE        , "pintool",  "w"    ,"0"    , "write miss allocate (0 for allocate, 1 for not allocate ");
KNOB<UINT32> KnobCacheDetailPrint(KNOB_MODE_WRITEONCE         , "pintool",  "dp"   ,"0"    , "Enable detailed private cache miss data");
KNOB<string> KnobSetType(KNOB_MODE_WRITEONCE                  , "pintool",  "r"    ,"LRU"  , "cache replacement policy");
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE               , "pintool",  "o"    ,"cout" , "specify icache file name");
KNOB<UINT64> KnobMaxSimInstCount(KNOB_MODE_WRITEONCE          , "pintool",  "insc" ,"5000000000", "Number of cores in the simulated system");
KNOB<string> KnobConfigFile(KNOB_MODE_WRITEONCE               , "pintool",  "c"    ,"/home/xtong/config.xml" , "specify simulation configuration file name");



/* ===================================================================== */
/* Initialization and Finalization */
/* ===================================================================== */
/// initialize_simobjs - initialize all the global objects of the simulator.
LOCALFUN VOID initialize_simobjs()
{
    simstats   = SIMSTATS::get_singleton();
    simxlator  = SIMXLATOR::get_singleton();
    simwait    = SIMPARAMS::get_singleton();
    simopts    = SIMOPTS::get_singleton();
    simglobals = SIMGLOBALS::get_singleton();
}

/// finalize_simobjs - destroy all the global objects of the simulator.
LOCALFUN VOID finalize_simobjs()
{
   delete simopts    ;
   delete simxlator  ;
   delete simwait    ;
   delete simstats   ;
   delete simglobals ;
}

/// initialize_simopts - initialize simulation options.
LOCALFUN VOID initialize_simopts()
{
    /// cache and tlb simulation options.
    simopts->set_replacepolicy(KnobSetType.Value());

    /// miscellaneous simulation options.
    simopts->set_tracerecord(KnobEnableTraceRecord.Value());
    simopts->set_maxsiminst(KnobMaxSimInstCount.Value());
    simopts->set_xml_parser(new ParseXML());
    simopts->get_xml_parser()->parse(KnobConfigFile.Value().c_str());
    simopts->set_ptwalk_trace(KnobEnablePTWalkTrace.Value());
    simopts->set_dynamic_addrspace_map(KnobEnableDynAddrSpaceMap.Value());
    simopts->set_static_addrspace_map(KnobEnableStaticAddrSpaceMap.Value());
    simopts->set_tlb_coherence(KnobEnableTLBCoherence.Value());
    simopts->set_ins_count(KnobEnableInsCount.Value());
}

LOCALFUN VOID initialize_simglobals() 
{
    clock_gettime(CLOCK_MONOTONIC, simglobals->get_time_init());
}

/// finalize_simopts - finalize the simulation options.
LOCALFUN VOID finalize_simopts()
{
    simopts->reset();
}

/// initialize_simwait - initialize simulation wait reasons.
LOCALFUN VOID initialize_simwait()
{
///    simwait->setwait(SIMPARAMS::WAIT_WORKER_THREAD);
}

/// main_module_init - initialize the main module of the simulator.
VOID main_module_init()
{
    initialize_simobjs();
    initialize_simopts();
    initialize_simwait();
    initialize_simglobals();
}

/// main_module_fini - finalize the main module of the simulator.
VOID main_module_fini() 
{
   finalize_simobjs();
   finalize_simopts();
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
    cerr <<"This pin tool implements multiple levels of caches and TLBs.\n\n";
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
