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

#ifndef PIN_PREDSIM_H
#define PIN_PREDSIM_H

#include "pin.H"
#include "common.hpp"
#include <string>


class PREDICTOR 
{
#define TAG_BITS 32 
#define MAX_MISPREDICT 2 
// prediction array.
typedef struct PSTRUCT {
    unsigned tag;
    unsigned pred;
    unsigned corp;
    unsigned misp;
    unsigned init;
} PSTRUCT;

public:
   /// prediction size.
   unsigned size;
   /// prediction and misprediction count.
   unsigned long long prediction;
   unsigned long long misprediction;
public:
   PSTRUCT* ptable;
public:
    ///@ constructor and destructor.
    PREDICTOR() {/* do nothing here */}
    PREDICTOR(unsigned psize) : size(psize), prediction(0), misprediction(0)
    {
       ptable = (PSTRUCT*) malloc(sizeof(PSTRUCT)*size);
       memset(ptable, 0x0, sizeof(PSTRUCT)*size);
    }
    virtual ~PREDICTOR() { free (ptable); }
   

    virtual int FindPred(ADDRINT iaddr) 
    {
        prediction ++;
        return ptable[iaddr & (size-1)].pred;
    }

    virtual void UpdateIncorrectPred(ADDRINT iaddr, ADDRINT value) 
    {
        ptable[iaddr & (size-1)].misp ++;
        ptable[iaddr & (size-1)].pred = value;
        ptable[iaddr & (size-1)].corp = 0;
    }

    virtual void UpdateCorrectPred(ADDRINT iaddr) 
    {  
       ptable[iaddr & (size-1)].corp ++;
    }

    ///@ prediction and misprediction count.
    virtual void Count()    { prediction ++;    }
    virtual void MisCount() { misprediction ++; }
    virtual void Reset()    { for(UINT i=0; i<size; ++i) ptable[i].misp = 0; }

    ///@ stats.
    virtual std::string StatsLong() 
    { 
       std::string rets;
       rets += "prediction size ";
       rets += mydecstr(size, 8);
       rets += " entries";
       rets += " ";
       rets += "predict is "; 
       rets +=  mydecstr(prediction, 8);
       rets += " ";
       rets += "mispredict is ";
       rets +=  mydecstr(misprediction, 8);
       rets += " ";
       rets += "misprediction accuracy is ";
       rets += StringDouble((double)misprediction/(1+prediction)*100) + "%";
       return rets; 
   }
};

/// @ confidence predictor. it can tell the confidence of the prediction.
class CONFIDENCE_PREDICTOR : public PREDICTOR
{
public:
    const unsigned correct_prediction = 2;
public:
    /// @ constructor and destructor.
    CONFIDENCE_PREDICTOR(unsigned psize) : PREDICTOR(psize) { /* do nothing */ }
    virtual ~CONFIDENCE_PREDICTOR() { /* do nothing */ }
    virtual int MakePred(ADDRINT iaddr)
    {
       return ptable[iaddr & (size-1)].corp >= correct_prediction;
    }

    // The only thing different from the PREDICTOR is that the confidence predictor
    // can tell the confidence of the prediction by tracking the last few predictions.
    virtual int UsePred(ADDRINT iaddr)
    {
       return MakePred(iaddr);
    }
};

// A few atypical branches will not influence the
// prediction (a better measure of the common case)
// Especially useful when multiple branches share the same
// counter (some bits of the branch PC are used to index
// into the branch predictor)
class BIMODAL_PREDICTOR : public PREDICTOR
{
private:
   const unsigned bits = 2;
   const unsigned true_threshold = (1<<(bits))/2;
   const unsigned lb = 0;
   const unsigned ub = 1<<bits;
public:
    ///@ constructor and destructor.
    BIMODAL_PREDICTOR() {/* do nothing here */}
    BIMODAL_PREDICTOR(unsigned psize) : PREDICTOR(psize) 
    {
          /* do nothing here */
    }
    virtual ~BIMODAL_PREDICTOR() { free (ptable); }
   
    /// @ update and find prediction.
    virtual void UpdatePred(ADDRINT iaddr, ADDRINT value) 
    {
        ptable[iaddr & (size-1)].init = 1;
        ptable[iaddr & (size-1)].tag  = iaddr & (TAG_BITS);
        /// if the branch is taken: counter = min(ub,counter+1)
        /// if the branch is not taken: counter = max(lb,counter-1)
        if (value)  
        {
        ptable[iaddr&(size-1)].pred = CACHESIM_MAX((ptable[iaddr&(size-1)].pred+1), ub);
        }
        else 
        ptable[iaddr&(size-1)].pred = CACHESIM_MIN((ptable[iaddr&(size-1)].pred-1), lb);

        /* update misprediction stats */
        if (ptable[iaddr & (size-1)].pred >= true_threshold && !value) misprediction ++;
    }
    virtual int FindPred(ADDRINT iaddr) 
    {
        prediction ++;
        unsigned res = ptable[iaddr & (size-1)].init ? ptable[iaddr & (size-1)].pred : 0;
        /* only predict taken if strongly taken */
        return res >= true_threshold;
    }
};

/// keep a global register of N bits.
class GSHARE_PREDICTOR : public PREDICTOR
{
private:
    const UINT16 shiftwidth = 12;
    const UINT64 shiftmax = (1<<shiftwidth)-1;
    UINT64 shiftreg;
private:
    ADDRINT GetIndex(ADDRINT iaddr) { return iaddr & shiftreg & shiftmax; }
    VOID UpdateShift(ADDRINT value) { shiftreg = (shiftreg << 1) | value; } 
public:
    ///@ constructor and destructor.
    GSHARE_PREDICTOR(unsigned psize) : PREDICTOR(psize) { shiftreg = 0;}
    ~GSHARE_PREDICTOR() { /* do nothing */ }

    /// @ find and update predictions.
    virtual int FindPred(ADDRINT iaddr) 
    {
        prediction ++;
        iaddr = GetIndex(iaddr);
        return ptable[iaddr & (size-1)].init ? ptable[iaddr & (size-1)].pred : 0;
    }
    virtual void UpdatePred(ADDRINT iaddr, ADDRINT value) 
    {
        iaddr = GetIndex(iaddr);
        /* update misprediction stats */
        if (ptable[iaddr & (size-1)].pred != value) misprediction ++;

        ptable[iaddr & (size-1)].init = 1;
        ptable[iaddr & (size-1)].tag  = iaddr & (TAG_BITS);
        ptable[iaddr & (size-1)].pred = value;

        /// we know the correct prediction at this point. update the shift register.
        UpdateShift(value);
    }
}; 

/// @ predict whether a translation is in the micro-tlb.
class TLBM_PREDICTOR : public CONFIDENCE_PREDICTOR
{
private:
    unsigned long long misprediction_in_tlbm;
    unsigned long long misprediction_not_in_tlbm;

    unsigned long long XlationMiss;
    unsigned long long XlationReduction;
public:
    ///@ constructor and destructor.
    TLBM_PREDICTOR(unsigned psize) : CONFIDENCE_PREDICTOR(psize), 
                                     misprediction_in_tlbm(0), 
                                     misprediction_not_in_tlbm(0),
                                     XlationMiss(0), 
                                     XlationReduction(0) 
    {
        /* intentionally left blank */
    }

    ///@ stats counters.
    void XlationDelayed(){ misprediction_not_in_tlbm ++; }
    void XlationMissed() { XlationMiss ++;               }
    void XlationReduce() { XlationReduction ++;          }

    ///@ stats.
    std::string StatsLong()
    {
       std::string rets = PREDICTOR::StatsLong();
       rets += "\ndelayed xlation ";
       rets += mydecstr(misprediction_not_in_tlbm, 8);
       rets += " ";
       rets += "\ndelayed xlation \% is";
       rets += " ";
       rets += StringDouble((double)misprediction_not_in_tlbm/(1+prediction)*100) + "%";
       rets += " ";
       rets += "\ntotal xlation miss is ";
       rets += " ";
       rets += mydecstr(XlationMiss, 8);
       rets += "\ntotal xlation miss \% is";
       rets += " ";
       rets += StringDouble((double)XlationMiss/(1+prediction)*100) + "%";
       rets += " ";
       rets += "\ntotal xlation reduction is";
       rets += " ";
       rets += mydecstr(XlationReduction, 8);
       rets += "\ntotal xlation reduction \% is";
       rets += " ";
       rets += StringDouble((double)XlationReduction/(1+prediction)*100) + "%";
 
       return rets;
   }
};

#endif // PIN_CACHESIM_H
