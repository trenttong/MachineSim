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
#include "caches.hh"
#include "utils.hh"
#include "predictor.hh"

#include <pthread.h>
#include <map>
#include <set>
#include <vector>
#include <algorithm>


/* ===================================================================== */
/* Globals variables */
/* ===================================================================== */
// Cache Simulation.
CACHE* il1 =  NULL;
CACHE* dl1 =  NULL;
CACHE* ul2 =  NULL;
CACHE* ul3 =  NULL;

// TLB simulation.
CACHE* itlbm =  NULL;
CACHE* dtlbm =  NULL;
CACHE* itlb1 =  NULL;
CACHE* dtlb1 =  NULL;
CACHE* utlb2 =  NULL;

// Coherence.
COHERENCE *tlbc = NULL;

// micro tlb hit prediction.
TLBM_PREDICTOR *mptlbm_1k = NULL;
TLBM_PREDICTOR *mptlbm_2k = NULL;
TLBM_PREDICTOR *mptlbm_4k = NULL;
TLBM_PREDICTOR *mptlbm_8k = NULL;
TLBM_PREDICTOR *iptlbm = NULL;

std::set<UINT32> **accesslist = NULL;
BOOL **accesslistActive = NULL;

UINT64 elided_mfence;
UINT64 executed_mfence;


// track the page that misses tlb and required a pagetable walk.
std::vector<ADDRINT> PTWalkTrace;

// track the activa pages in the address space.
std::map<ADDRINT, UINT32> ActivePages;

typedef
VOID (*INVOKE_INS_REF_PROC) (ADDRINT, THREADID);

typedef
VOID (*INVOKE_MEM_REF_PROC) 
(ADDRINT, ADDRINT, UINT32, CACHE_BASE::ACCESS_TYPE, UINT64, UINT64,  THREADID);


/* ===================================================================== */
/* Definition Flags */
/* ===================================================================== */

/* ===================================================================== */
/* Cache Simulation Routines */
/* ===================================================================== */
bool CACHE::Access(ADDRINT iaddr, ADDRINT addr, UINT32 size, ACCESS_TYPE type, THREADID tid)
{
    const ADDRINT highAddr = addr + size;
    bool allHit = true;

    const ADDRINT lineSize = GetLineSize();
    const ADDRINT notLineMask = ~(lineSize - 1);
    do
    {
    CACHE_TAG tag;
    UINT32 setindex;

    SplitAddress(addr, tag, setindex);

    CacheImpl *cache = GetCache(tid);
    CACHE_SET_BASE *set = cache->CacheSets[setindex];

    bool localHit = set->Find(tag);

    allHit &= localHit;

    // on miss, loads always allocate, stores optionally
    if (!localHit && (type == ACCESS_TYPE_LOAD || CacheStoreAlloc == CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE))
    {
        CACHE_TAG etag;
        set->Replace(tag, etag, iaddr);
        EvictPrev(etag.CacheTag, tid);
    }
    addr = (addr & notLineMask) + lineSize; // start of next cache line
    } while (addr < highAddr);

    GetCache(tid)->CacheAccess[type][allHit]++;

    return allHit;
}

bool CACHE::AccessSingleLine(ADDRINT iaddr, ADDRINT addr, ACCESS_TYPE type, THREADID tid)
{
    CACHE_TAG tag;
    UINT32 setindex;

    SplitAddress(addr, tag, setindex);

    CACHE_SET_BASE* set = GetCache(tid)->CacheSets[setindex];

    bool hit = set->Find(tag);

    // on miss, loads always allocate, stores optionally
    if (!hit && (type == ACCESS_TYPE_LOAD || CacheStoreAlloc == CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE))
    {
        CACHE_TAG etag;
        set->Replace(tag, etag, iaddr);
        EvictPrev(etag.CacheTag, tid);
    }

    GetCache(tid)->CacheAccess[type][hit]++;
    return hit;
}

bool CACHE::AccessPage(ADDRINT addr, ACCESS_TYPE type, THREADID tid)
{
    // last 12 bits does not matter.
    UINT32 setindex = GETPAGE(addr) & (CacheMaxSets-1);
    CACHE_TAG tag = GETPAGE(addr);

    CACHE_SET_BASE* set = GetCache(tid)->CacheSets[setindex];

    bool hit = set->Find(tag);

    // on miss, loads always allocate, stores optionally
    if (!hit && (type == ACCESS_TYPE_LOAD || CacheStoreAlloc == CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE))
    {
        CACHE_TAG etag;
        set->Replace(tag,etag, 0);
        // EvictPrev(etag.CacheTag, tid);
        if (tlbc) tlbc->AddOwner(tag.CacheTag, tid);
        if (tlbc) tlbc->SubOwner(etag.CacheTag, tid);
    }

    GetCache(tid)->CacheAccess[type][hit]++;
    return hit;
}


/* ===================================================================== */
/* Cache Access Routines */
/* ===================================================================== */

LOCALFUN VOID CACHE_Ul3Access(ADDRINT  iaddr               , 
                              ADDRINT  addr                , 
                              UINT32   size                , 
                              CACHE_BASE::ACCESS_TYPE type , 
                              THREADID tid                 )
{
    if (!simwait->dosim()) return;

    // third level unified cache
    // level 3 cache is shared ... it could be access concurrently by different threads.
    simglobals->get_global_simlock()->lock_l3_cache(tid);
    if (ul3) ul3->Access(iaddr, addr, size, type, tid);
    simglobals->get_global_simlock()->unlock_l3_cache(tid);
    return;
}

LOCALFUN VOID CACHE_Ul2Access(ADDRINT  iaddr               , 
                              ADDRINT  addr                , 
                              UINT32   size                , 
                              CACHE_BASE::ACCESS_TYPE type , 
                              THREADID tid                 )
{
    if (!simwait->dosim()) return;

    // second level unified cache
    BOOL ul2Hit = 0;
    if (!ul2Hit && ul2) ul2Hit = ul2->Access(iaddr, addr, size, type, tid);
    if (!ul2Hit && ul3) CACHE_Ul3Access(iaddr, addr, size, type, tid);
    return;
}

/* ===================================================================== */
///@ this function analyze whether tlb demostrate trace pattern.
/* ===================================================================== */
LOCALFUN BOOL Sort_TLB_MemCount(std::pair<ADDRINT,INT> f, std::pair<ADDRINT,INT> s)
{
    return f.second < s.second;
}

LOCALFUN VOID Analyze_TLB_MemAccess(std::ofstream &out)
{
    const unsigned scale = 10000;
    const unsigned lookahead  = 5;
    const int fthreshold = 5;
    std::map<ADDRINT, INT> PTWalkCount;

    out << "******************** PageTable Walk Trace Analysis **********************\n";

    for (std::vector<ADDRINT>::iterator I=PTWalkTrace.begin(), E=PTWalkTrace.end(); I!=E; ++I)
    {
        PTWalkCount[*I] ++;
    }

    // Get total miss count.
    UINT64 misscount = 0;
    for (std::map<ADDRINT, INT>::iterator I=PTWalkCount.begin(), E=PTWalkCount.end(); I!=E; ++I)
    {
        misscount += I->second;
    }

    // find the followers.
    for (std::map<ADDRINT, INT>::iterator I=PTWalkCount.begin(), E=PTWalkCount.end(); I!=E; ++I)
    {
        if ((double)I->second/misscount*scale<1) continue;
     
        std::map<ADDRINT, INT> followers;
        followers.clear();
        // find all its followers.
        for (std::vector<ADDRINT>::iterator J=PTWalkTrace.begin(), K=PTWalkTrace.end(); J!=K; ++J)
        {
          if (I->first==*J) for (UINT i=0; i<lookahead; ++i) followers[*++J]++;
        }

        out << "***Leader:   " << std::hex << "0x" << I->first << ":" << std::dec << I->second << endl;
        out << "***Followers:" << endl;
        for (std::map<ADDRINT, INT>::iterator M=followers.begin(), N=followers.end(); M!=N; ++M)
        {
          if (M->second>fthreshold) out << "0x" << std::hex << M->first << ":" <<  std::dec << M->second << endl;
        }
        out << endl << endl;
    }
    return;
}

/* ===================================================================== */
///@ this function implements a address space mapping module.
/* ===================================================================== */
LOCALFUN VOID AddrSpace_MemAccess(ADDRINT  addr                  , 
                                  UINT32   size                  , 
                                  CACHE_BASE::ACCESS_TYPE type   , 
                                  THREADID tid                   )
{
    if (!simwait->dosim()) return;
    ActivePages[GETPAGE(addr)] = 1;
    return;
}

LOCALFUN VOID AddrSpace_MemAccess_Print(std::ofstream &out)
{
   out << "#==================\n" << "# AddressSpace stats\n" << "#====================\n";
   ADDRINT DwReg=(*ActivePages.begin()).first;
   std::map<ADDRINT, UINT32>::iterator I,M,E;
   UINT32 ChunkSize = 0, ChunkCount = 0;
   M=ActivePages.begin(); M++;
   for(I=ActivePages.begin(), E=ActivePages.end(); M!=E; I++, M++)
   {
       if (((*I).first+1)==(*M).first) continue;
       out << "Region starts from 0x" << std::hex << DwReg << " and ends at 0x" << (*I).first 
           << " for a size of " << std::dec << ((*I).first-DwReg)*PAGESIZE/KILO << " KB" << endl;
       ChunkCount ++;
       ChunkSize += ((*I).first-DwReg)*PAGESIZE/KILO;
       DwReg = (*M).first;
   }

   out << "Average chunk size is " << (double) ChunkSize/ChunkCount << endl;
   return;
}

/* ===================================================================== */
///@ this function simulates TLB accesses.
/* ===================================================================== */
LOCALFUN VOID TLB_MemAccess(ADDRINT  addr                  , 
                            CACHE_BASE::ACCESS_TYPE type   , 
                            THREADID tid                   )
{
    if (!simwait->dosim()) return;
    if (simopts->get_ptwalk_trace()) PTWalkTrace.push_back(addr);
    return;
}

LOCALFUN VOID TLB_Ul2Access(ADDRINT  addr                  , 
                            CACHE_BASE::ACCESS_TYPE type   , 
                            THREADID tid                   )
{
    if (!simwait->dosim()) return;

    BOOL ul2Hit = 0;
    if (utlb2 && !ul2Hit) ul2Hit = utlb2->AccessPage(addr, type, tid);
    if (!ul2Hit) TLB_MemAccess(addr, type, tid);
    
    return;
}

LOCALFUN VOID MicroTLB_Predict(TLBM_PREDICTOR *ptlbm, ADDRINT iaddr, ADDRINT tag, BOOL tlbHit)
{
    BOOL use  = ptlbm->UsePred(iaddr);
    BOOL pred = ptlbm->FindPred(iaddr);
    if (pred)
    {
       // Prediction says the translation is in the micro-tlb. In fact it is not.
       if (!tlbHit) 
       {
       if (use) ptlbm->XlationDelayed();
       ptlbm->UpdateIncorrectPred(iaddr, tlbHit);
       }
       else
       {
       if (use) ptlbm->XlationReduce();
       ptlbm->UpdateCorrectPred(iaddr);
       }
    } else {
       // Prediction says its not in the microtlb. in fact it is.
       if (tlbHit) 
       {
       ptlbm->XlationMissed();
       ptlbm->UpdateIncorrectPred(iaddr, tlbHit);
       }
       else ptlbm->UpdateCorrectPred(iaddr);
    }
    return;
}

LOCALFUN BOOL LastBlock(ADDRINT addr,  THREADID tid)
{
    // decode a block at a time.
    static ADDRINT lastblock[MAX_CACHE_THREAD];
    BOOL last = (GETBLOCK(addr) == lastblock[tid]);
    if (!last) lastblock[tid] = GETBLOCK(addr);
    return last;
}

LOCALFUN VOID InsRefBlock(ADDRINT addr                 , 
                          THREADID tid                 )
{

    // decode a block at a time.
    if (!simwait->dosim()) return;

    const CACHE_BASE::ACCESS_TYPE type = CACHE_BASE::ACCESS_TYPE_LOAD;
    BOOL iche_hit = 0;
    BOOL itlb_hit = 0;
    /// ================================================== ///
    /* simulate icache. */
    /// ================================================== ///
    if (!iche_hit) iche_hit = il1->AccessSingleLine(addr, addr, type, tid);
    if (!iche_hit) CACHE_Ul2Access(addr, addr, 1, type, tid);

    /// ================================================== ///
    /* simulate TLB. */
    /// ================================================== ///
    if (!itlb_hit && itlbm) itlb_hit = itlbm->AccessPage(addr, type, tid);
    if (!itlb_hit && itlb1) itlb_hit = itlb1->AccessPage(addr, type, tid);
    if (!itlb_hit) TLB_Ul2Access(addr, type, tid);

    return;
}

LOCALFUN VOID MemRefMulti(ADDRINT  iaddr              , 
                          ADDRINT  addr               , 
                          UINT32   size               , 
                          CACHE_BASE::ACCESS_TYPE type, 
                          UINT64   basereg            , 
                          UINT64   idxreg             , 
                          THREADID tid                )
{
    // waiting for simulation to start.
    if (!simwait->dosim()) return;

    if (simopts->get_dynamic_addrspace_map()) AddrSpace_MemAccess(addr, size, type, tid);

    BOOL dche_hit = 0;
    BOOL dtlb_hit = 0;
    /// ================================================== ///
    /* simulate dcache. */
    /// ================================================== ///
    if (!dche_hit && dl1) dche_hit = dl1->Access(iaddr, addr, size, type, tid);
    if (!dche_hit) CACHE_Ul2Access(iaddr, addr, size, type, tid);

    /// ================================================== ///
    /* simulate dtlb */
    /// ================================================== ///
    if (!dtlb_hit && dtlbm) dtlb_hit = dtlbm->AccessPage(addr, type, tid);
    if (!dtlb_hit && dtlb1) dtlb_hit = dtlb1->AccessPage(addr, type, tid);
    if (!dtlb_hit) TLB_Ul2Access(addr, type, tid);

    return;
}

LOCALFUN VOID MemRefSingle(ADDRINT   iaddr             , 
                           ADDRINT   addr              ,
                           UINT32    size              , 
                           CACHE_BASE::ACCESS_TYPE type, 
                           UINT64    basereg           , 
                           UINT64    idxreg            ,  
                           THREADID  tid               )
{
    // waiting for simulation to start.
    if (!simwait->dosim()) return;

    if (simopts->get_dynamic_addrspace_map()) AddrSpace_MemAccess(addr, size, type, tid);

    BOOL dche_hit = 0;
    BOOL dtlb_hit = 0;
    /// ================================================== ///
    /* simulate dcache */
    /// ================================================== ///
    if (!dche_hit && dl1) dche_hit = dl1->AccessSingleLine(iaddr, addr, type, tid);
    if (!dche_hit) CACHE_Ul2Access(iaddr, addr, size, type, tid);

    /// ================================================== ///
    /* simulate dtlb */
    /// ================================================== ///
    if (!dtlb_hit && dtlbm) dtlb_hit = dtlbm->AccessPage(addr, type, tid);
    if (!dtlb_hit && dtlb1) dtlb_hit = dtlb1->AccessPage(addr, type, tid);
    if (!dtlb_hit) utlb2->AccessPage(addr, type, tid);
}


/* ===================================================================== */
/* Called by the instruction.cpp module */
/* ===================================================================== */
VOID InsFetchRef(ADDRINT  addr                         , 
                 THREADID tid                          )
{
    const INVOKE_INS_REF_PROC simFun = (INVOKE_INS_REF_PROC) InsRefBlock;
    simFun(addr, tid);
    return;
}

VOID DataFetchRef(ADDRINT  iaddr                       , 
                  ADDRINT  addr                        , 
                  UINT32   size                        , 
                  UINT64   baseval                     , 
                  UINT64   idxval                      , 
                  THREADID tid                         )
{
    const INVOKE_MEM_REF_PROC simFun = (size <= 4 ? 
                                       (INVOKE_MEM_REF_PROC) MemRefSingle :
                                       (INVOKE_MEM_REF_PROC) MemRefMulti) ;
    simFun(iaddr, addr, size, CACHE_BASE::ACCESS_TYPE_LOAD, baseval, idxval, tid);
    return;
}

VOID DataWriteRef(ADDRINT  iaddr                       , 
                  ADDRINT  addr                        , 
                  UINT32   size                        , 
                  UINT64   baseval                     , 
                  UINT64   idxval                      , 
                  THREADID tid                         )
{
    const INVOKE_MEM_REF_PROC simFun = (size <= 4 ? 
                                       (INVOKE_MEM_REF_PROC) MemRefSingle :
                                       (INVOKE_MEM_REF_PROC) MemRefMulti) ;
    simFun(iaddr, addr, size, CACHE_BASE::ACCESS_TYPE_STORE, baseval, idxval, tid);

    if (tlbc->SingleOwner(addr)) simglobals->get_global_simlowl()->atom_uint64_inc(&elided_mfence);
    else simglobals->get_global_simlowl()->atom_uint64_inc(&executed_mfence);

    return;
}

/* ===================================================================== */
/* Printing Routines */
/* ===================================================================== */
LOCALFUN VOID cache_and_tlb_module_print()
{
    char name[128];
    sprintf(name, "%s.%d", "cache_sim.out", PIN_GetPid());
    std::ofstream out(name);

    out << "#==================\n" << "# General stats\n" << "#====================\n";
    out << "# :" << endl;
    out << "# " << simglobals->get_global_icount() << " instructions executed\n";
    out << "# " << endl;

    if (il1)   out << il1->StatsParam();
    if (dl1)   out << dl1->StatsParam();
    if (ul2)   out << ul2->StatsParam();
    if (ul3)   out << ul3->StatsParam();
    if (dtlbm) out << dtlbm->StatsParam();
    if (itlbm) out << itlbm->StatsParam();
    if (dtlb1) out << dtlb1->StatsParam();
    if (itlb1) out << itlb1->StatsParam();
    if (utlb2) out << utlb2->StatsParam();
    out << "\n\n";

    if (il1)
    {
    out << "################\n" << "# L1 ICACHE stats\n" << "################\n";
    out << il1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (dl1)
    {
    out << "################\n" << "# L1 DCACHE stats\n" << "################\n";
    out << dl1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (ul2)
    {
    out << "################\n" << "# L2 unified CACHE stats\n" << "################\n";
    out << ul2->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (ul3)
    {
    out << "################\n" << "# L3 unified CACHE stats\n" << "################\n";
    out << ul3->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE, 0);
    }
    if (itlbm)
    {
    out << "################\n" << "# 4. Micro 4K ITLB stats\n" << "################\n";
    out << itlbm->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (dtlbm)
    {
    out << "################\n" << "# 4. Micro 4K DTLB stats\n" << "################\n";
    out << dtlbm->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (itlb1)
    {
    out << "################\n" << "# L1 4K TLB stats\n" << "################\n";
    out << itlb1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (dtlb1)
    {
    out << "################\n" << "# L1 4K TLB stats\n" << "################\n";
    out << dtlb1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (utlb2) 
    {
    out << "################\n" << "# L2 TLB stats\n" << "################\n";
    out << utlb2->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
    }
    if (tlbc)
    {
    out << "################\n" << "# TLB Coherence\n" << "################\n";
    out << tlbc->StatsLong();
    }

    if (simopts->get_ptwalk_trace())  Analyze_TLB_MemAccess(out);
    if (simopts->get_dynamic_addrspace_map()) AddrSpace_MemAccess_Print(out);

    out << "# elided mfence " << elided_mfence << " executed_mfence " << executed_mfence << endl;

    /* done */
    fprintf(stdout, "simulation stats dumped into %s.%d\n", "cache_sim.out", PIN_GetPid());
    return;
}

/* ===================================================================== */
/* Initialization and Finalization Routines */
/* ===================================================================== */

/// init_sim_cache - initialize cache module.
VOID cache_and_tlb_module_init()
{
    if (simopts->get_xml_parser()->sys.L1_icache.cache_enable)
    {
        il1 = new CACHE("Micro 4K TLB Cache", 1,
                        simopts->get_xml_parser()->sys.L1_icache.number_entries*
                        simopts->get_xml_parser()->sys.L1_icache.cache_linesize,
                        simopts->get_xml_parser()->sys.L1_icache.cache_linesize,
                        simopts->get_xml_parser()->sys.L1_icache.associativity,
                        simopts->get_replacepolicy(),
                        CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                        0);

    }
    if (simopts->get_xml_parser()->sys.L1_dcache.cache_enable)
    {
        dl1 = new CACHE("Micro 4K TLB Cache", 1,
                        simopts->get_xml_parser()->sys.L1_dcache.number_entries*
                        simopts->get_xml_parser()->sys.L1_dcache.cache_linesize,
                        simopts->get_xml_parser()->sys.L1_dcache.cache_linesize,
                        simopts->get_xml_parser()->sys.L1_dcache.associativity,
                        simopts->get_replacepolicy(),
                        CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                        0);

    }
    if (simopts->get_xml_parser()->sys.L2_ucache.cache_enable)
    {
        ul2 = new CACHE("Micro 4K TLB Cache", 1,
                        simopts->get_xml_parser()->sys.L2_ucache.number_entries*
                        simopts->get_xml_parser()->sys.L2_ucache.cache_linesize,
                        simopts->get_xml_parser()->sys.L2_ucache.cache_linesize,
                        simopts->get_xml_parser()->sys.L2_ucache.associativity,
                        simopts->get_replacepolicy(),
                        CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                        0);
        ul2->SetPrev(il1);
        ul2->SetPrev(dl1);
    }
    if (simopts->get_xml_parser()->sys.L3_ucache.cache_enable)
    {
        ul3 = new CACHE("Micro 4K TLB Cache", 1,
                        simopts->get_xml_parser()->sys.L3_ucache.number_entries*
                        simopts->get_xml_parser()->sys.L3_ucache.cache_linesize,
                        simopts->get_xml_parser()->sys.L3_ucache.cache_linesize,
                        simopts->get_xml_parser()->sys.L3_ucache.associativity,
                        simopts->get_replacepolicy(),
                        CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                        0);
        ul3->SetPrev(il1);
    }
    if (simopts->get_xml_parser()->sys.LM_itlb.cache_enable)
    {
        itlbm = new CACHE("Micro 4K TLB Cache", 1,
                          simopts->get_xml_parser()->sys.LM_itlb.number_entries*
                          simopts->get_xml_parser()->sys.LM_itlb.cache_linesize,
                          simopts->get_xml_parser()->sys.LM_itlb.cache_linesize,
                          simopts->get_xml_parser()->sys.LM_itlb.associativity,
                          simopts->get_replacepolicy(),
                          CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                          0);
    }
    if (simopts->get_xml_parser()->sys.LM_itlb.cache_enable)
    {
        dtlbm = new CACHE("Micro 4K TLB Cache", 1,
                          simopts->get_xml_parser()->sys.LM_dtlb.number_entries*
                          simopts->get_xml_parser()->sys.LM_dtlb.cache_linesize,
                          simopts->get_xml_parser()->sys.LM_dtlb.cache_linesize,
                          simopts->get_xml_parser()->sys.LM_dtlb.associativity,
                          simopts->get_replacepolicy(),
                          CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                          0);
    }
    if (simopts->get_xml_parser()->sys.L1_itlb.cache_enable)
    {
        itlb1 = new CACHE("Micro 4K TLB Cache", 1,
                          simopts->get_xml_parser()->sys.L1_itlb.number_entries* 
                          simopts->get_xml_parser()->sys.L1_itlb.cache_linesize,
                          simopts->get_xml_parser()->sys.L1_itlb.cache_linesize,
                          simopts->get_xml_parser()->sys.L1_itlb.associativity,
                          simopts->get_replacepolicy(),
                          CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                          0);
        itlb1->SetPrev(itlbm);
    }
    if (simopts->get_xml_parser()->sys.L1_dtlb.cache_enable)
    {
        dtlb1 = new CACHE("Micro 4K TLB Cache", 1,
                          simopts->get_xml_parser()->sys.L1_dtlb.number_entries*
                          simopts->get_xml_parser()->sys.L1_dtlb.cache_linesize,
                          simopts->get_xml_parser()->sys.L1_dtlb.cache_linesize,
                          simopts->get_xml_parser()->sys.L1_dtlb.associativity,
                          simopts->get_replacepolicy(),
                          CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                          0);

        dtlb1->SetPrev(dtlbm);
    }
    if (simopts->get_xml_parser()->sys.L2_utlb.cache_enable)
    {
        utlb2 = new CACHE("Micro 4K TLB Cache", 1,
                          simopts->get_xml_parser()->sys.L2_utlb.number_entries*   
                          simopts->get_xml_parser()->sys.L2_utlb.cache_linesize,
                          simopts->get_xml_parser()->sys.L2_utlb.cache_linesize,
                          simopts->get_xml_parser()->sys.L2_utlb.associativity,
                          simopts->get_replacepolicy(),
                          CACHE::CACHE_STORE::CACHE_STORE_ALLOCATE,
                          0);
        utlb2->SetPrev(dtlb1);
        utlb2->SetPrev(itlb1);
    }
    if (simopts->get_tlb_coherence()) tlbc = new COHERENCE();

    // done. 
    return;
}

VOID cache_and_tlb_module_fini()
{
    // print cache simulation results.
    cache_and_tlb_module_print();

    // free cache simulation resources.
    if (il1)    delete il1;
    if (dl1)    delete dl1;
    if (ul2)    delete ul2;
    if (ul3)    delete ul3;
    if (itlbm)  delete itlbm;
    if (dtlbm)  delete dtlbm;
    if (itlb1)  delete itlb1;
    if (dtlb1)  delete dtlb1;
    if (utlb2)  delete utlb2;
    if (tlbc)   delete tlbc;
}

#if 0
/////////////////////////// code recycling bin   /////////////////////////////// 
        // Check the micro-tlb prediction.
        MicroTLB_Predict(mptlbm_1k, iaddr, GETPAGE(basereg), tlbHit);
        MicroTLB_Predict(mptlbm_2k, iaddr, GETPAGE(basereg), tlbHit);
        MicroTLB_Predict(mptlbm_4k, iaddr, GETPAGE(basereg), tlbHit);
        MicroTLB_Predict(mptlbm_8k, iaddr, GETPAGE(basereg), tlbHit);

    out << "################\n" << "# Micro 4K DTLB Prediction stats\n" << "################\n";
    out << mptlbm_1k->StatsLong() << endl;

    out << "################\n" << "# Micro 4K DTLB Prediction stats\n" << "################\n";
    out << mptlbm_2k->StatsLong() << endl;

    out << "################\n" << "# Micro 4K DTLB Prediction stats\n" << "################\n";
    out << mptlbm_4k->StatsLong() << endl;

    out << "################\n" << "# Micro 4K DTLB Prediction stats\n" << "################\n";
    out << mptlbm_8k->StatsLong() << endl;

    out << "################\n" << "# Micro 4K ITLB Prediction stats\n" << "################\n";
    out << iptlbm->StatsLong() << endl;
#endif


#if 0
    PageRecord *page = NULL;
    if ((simopts->get_detailpagestats()))
    {
      page = GetPageRecord(tag);
      page->AccessCount ++;
      page->BlockAccessCount[GETSUBBLOCK(addr)] ++;
    }

        if ((page)) page->InstallCount ++;
        if ((page)) page->BlockMissCount[GETSUBBLOCK(addr)] ++;
#endif


