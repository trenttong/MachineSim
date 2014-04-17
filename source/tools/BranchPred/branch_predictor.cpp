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
#include <iostream>

#include "pin.H"
#include "instlib.H"
#include "bimodal.H"
#include "gshare.H"
#include "simpleindir.H"
#include "ttc.H"

using namespace INSTLIB;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */
KNOB<BOOL> KnobDisableBimodal(KNOB_MODE_WRITEONCE, "pintool",
    "db","0", "Disable bimodal branch predictor");
KNOB<BOOL> KnobDisableGshare(KNOB_MODE_WRITEONCE, "pintool",
    "dg","0", "Disable gshare branch predictor");
KNOB<BOOL> KnobDisableSimpleIndirect(KNOB_MODE_WRITEONCE, "pintool",
    "ds","0", "Disable simple indirect branch predictor");
KNOB<BOOL> KnobDisableTargetCache(KNOB_MODE_WRITEONCE, "pintool",
    "dt","0", "Disable two level target cache branch predictor");
KNOB<BOOL> KnobDisableICount(KNOB_MODE_WRITEONCE, "pintool",
    "di","0", "Disable instruction count");
KNOB<UINT32> KnobSingleThread(KNOB_MODE_WRITEONCE, "pintool",
    "st","0", "Enable single thread profile");
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "predictor.out", "specify predictor file name");

/* ===================================================================== */
/* Branch Predictors */
/* ===================================================================== */
LOCALVAR BIMODAL bimodal;
LOCALVAR GSHARE  gshare;
LOCALVAR SIMPLEINDIR simpleindirect;
LOCALVAR TARGETCACHE targetcache;
LOCALVAR ofstream *outfile;

/* ===================================================================== */
/* Other resources */
/* ===================================================================== */
// Track the number of instructions executed
ICOUNT icount;

/* ===================================================================== */
/* Printing Routines */
/* ===================================================================== */
INT32 Usage()
{
  cerr <<"This pin tool implements several branch predictors\n\n";
  cerr << KNOB_BASE::StringKnobSummary() << endl;
  return -1;
}

LOCALFUN VOID Fini(int n, void *v)
{
  *outfile << "============== Branch Prediction Results  ==============\n";

  int index = 0;
  *outfile << endl;
  
  // DumpInfo predictor statisticsi and free predictor memory.
  if (!KnobDisableBimodal.Value())
     {
     *outfile << "\n=============  Bimodal Branch Prediction Results  ==============\n\n";
     for(index = 0; index < MAX_PREDICTOR_THREAD; index ++)
        {
        bimodal.PredictorData[index].DumpInfo(outfile, &icount);
        bimodal.PredictorData[index].Shutdown();
        }
     }
  if (!KnobDisableGshare.Value())
     {
     *outfile << "\n=============  Gshare Branch Prediction Results  ==============\n\n";
     for(index = 0; index < MAX_PREDICTOR_THREAD; index ++)
        {
        gshare.PredictorData[index].DumpInfo(outfile, &icount);
        gshare.PredictorData[index].Shutdown();
        }
     }
  if (!KnobDisableSimpleIndirect.Value())
     {
     *outfile << "\n=============  Simple Indirect Branch Prediction Results  ==============\n\n";
     for(index = 0; index < MAX_PREDICTOR_THREAD; index ++)
        {
        simpleindirect.PredictorData[index].DumpInfo(outfile, &icount);
        simpleindirect.PredictorData[index].Shutdown();
        }
     }
  if (!KnobDisableTargetCache.Value())
     {
     *outfile << "\n=============  Target Cache Branch Prediction Results  ==============\n\n";
     for(index = 0; index < MAX_PREDICTOR_THREAD; index ++)
        {
        targetcache.PredictorData[index].DumpInfo(outfile, &icount);
        targetcache.PredictorData[index].Shutdown();
        }
     }
  return;
}

/* ===================================================================== */
/* The fun begins ... */
/* ===================================================================== */
int main(int argc, char *argv[])
{
  if (PIN_Init(argc,argv)) return Usage();

  // Single thread profile enabled ?
  if (KnobSingleThread.Value())
     {
     bimodal.SetSingleOSThreadID(KnobSingleThread.Value());
     gshare.SetSingleOSThreadID(KnobSingleThread.Value());
     simpleindirect.SetSingleOSThreadID(KnobSingleThread.Value());
     targetcache.SetSingleOSThreadID(KnobSingleThread.Value());
     }

  // Active the predictors.
  if (!KnobDisableBimodal.Value())
     {
     bimodal.Activate();
     }
  if (!KnobDisableGshare.Value())
     {
     gshare.Activate();
     }
  if (!KnobDisableSimpleIndirect.Value())
     {
     simpleindirect.Activate();
     }
  if (!KnobDisableTargetCache.Value())
     {
     targetcache.Activate();
     }

  // Activate the icount.
  if (!KnobDisableICount.Value()) 
     {
     icount.Activate();
     }

  outfile = new ofstream(KnobOutputFile.Value().c_str());

  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
}
