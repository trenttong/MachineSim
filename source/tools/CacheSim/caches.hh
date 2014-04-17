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

#ifndef PIN_CACHESIM_H
#define PIN_CACHESIM_H

#include "pin.H"
#include "utils.hh"

typedef UINT64 CACHE_STATS; // type of cache hit/miss counters

#include <cassert>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <stdlib.h>
#include <numeric>
#include <map>
#include <list>
#include <algorithm>

#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>

class CACHE_TAG;
class CACHE;
class CACHE_BASE;
class CACHE_LRU_SET;
class CACHE_BASE_SET;


/// @ CACHE_TAG - this class implements a tag in the cache.
class CACHE_TAG
{
public:
    ADDRINT CacheTag;
public:
    operator ADDRINT() const            { return CacheTag;               }
    CACHE_TAG(ADDRINT tag = 0)          { CacheTag = tag;                }
    bool unused() const                 { return CacheTag == 0;          } 
    bool operator==(CACHE_TAG &r) const { return CacheTag == r.CacheTag; }
};

//// PageRecord - record information of a page.
class PageRecord
{
public:
    /// tag/address of the page.
    CACHE_TAG tag;
    /// number of times the page is accessed.
    UINT64 AccessCount;
    /// number of times the page is refilled.
    UINT64 InstallCount;
    UINT32 BlockMissCount[4096/64];
    UINT32 BlockAccessCount[4096/64];

public:
    PageRecord() : tag(0), AccessCount(0), InstallCount(0)
    {
        for (INT i=0; i<4096/64; i++) BlockMissCount[i] = 0;
        for (INT i=0; i<4096/64; i++) BlockAccessCount[i] = 0;
    }
    PageRecord(CACHE_TAG tag) : tag(tag), AccessCount(0), InstallCount(0)
    {
        for (INT i=0; i<4096/64; i++) BlockMissCount[i] = 0;
        for (INT i=0; i<4096/64; i++) BlockAccessCount[i] = 0;
    }

    string PageStats(UINT64 refills)
    {
        string out;
        out += "# ";
        out += StringHex(tag.CacheTag << 12);
        out += "	";
        out += StringInt(AccessCount);
        out += "	";
        out += StringInt(InstallCount);
        out += "	";
        out += StringInt(AccessCount/InstallCount);
        out += "	";
        out += StringDouble(100*(double)InstallCount/refills);
        out += "%";
        out += "\n";
        return out;
    }

    string BlockAccessStats()
    {
        string out;
        out += "# ";
        INT64 count=0;
        for (INT i=0; i<4096/64; i++)
        {
            out += StringInt(BlockAccessCount[i]);
            out += " ";
            count += BlockAccessCount[i];
        }
        out +=  StringInt(count);
        out += "\n";
        return out;
    }


    string BlockMissStats()
    {
        string out;
        out += "# ";
        INT64 count=0;
        for (INT i=0; i<4096/64; i++)
        {
            out += StringInt(BlockMissCount[i]);
            out += " ";
            count += BlockMissCount[i];
        }
        out +=  StringInt(count);
        out += "\n";
        return out;
    }

};
bool SortByAccessCount(PageRecord &rec1, PageRecord& rec2)
{
    return rec1.AccessCount > rec2.AccessCount;
}

bool SortByRefillCount(PageRecord &rec1, PageRecord& rec2)
{
    return rec1.InstallCount > rec2.InstallCount;
}

bool SortByAddress(PageRecord &rec1, PageRecord& rec2)
{
    return rec1.tag.CacheTag > rec2.tag.CacheTag;
}


#define FOREACH_CACHE(X)        for(INT index=0;index<MAX_CACHE_THREAD;++index) {X;}
#define FOREACH_CACHEWAY(X)     for(INT index=0;index<CacheAssoc;++index) {X;}
#define FOREACH_CACHEACCESS(X)  for(INT index=0;index<ACCESS_TYPE_NUM;++index) {X;}
#define FOREACH_CACHE_SUM(X)    do {                          \
   INT64 sum = 0;                                             \
   for(INT index=0;index<MAX_CACHE_THREAD;++index) {sum+=X;}  \
   return sum;                                                \
} while(0);
#define FOREACH_CACHEACCESS_SUM(X)  do {                      \
   INT64 sum = 0;                                             \
   for(INT index=0;index<ACCESS_TYPE_NUM;++index) {sum+=X;}   \
   return sum;                                                \
} while(0);

/// @ CACHE_SET - Everything related to cache sets
//  @
//  @ To specify a cache set. Parameters which need to be specified are listed below.
//  @
//  @ - associativity - how many lines are in the set
//  @ - replacement   - what is the replacement policy for the set.
/* ===================================================================== */
/* Cache Sets ... */
/* ===================================================================== */

/// @ CACHE_SET_BASE - This is the base of all cache sets. 
class CACHE_SET_BASE
{
protected:
    // associativity of the cache set. 
    INT32 CacheAssoc;
    // all the tags in the cache size.
    CACHE_TAG *CacheTags;
public:
    CACHE_BASE *CacheBase;
public:
    /// @ constructor.
    CACHE_SET_BASE(UINT32 assoc             , 
                   UINT32 level             ,
                   CACHE_BASE* cache        )
                   : 
                   CacheAssoc(assoc)        ,
                   CacheTags(0)             , 
                   CacheBase(cache)
    {
       CacheTags  = new CACHE_TAG[CacheAssoc];
    }

    /// @ destructor.
    virtual ~CACHE_SET_BASE() { /*free (CacheTags);*/ }

    /// @ GetUnused - get the free entry in the cache set.
    INT32 GetUnused() const
    {
        FOREACH_CACHEWAY(if (CacheTags[index].unused()) return index;);
        return CacheAssoc;
    }

    // Find and Replace are the only two functions that need to be specialized.
    // Usually the Find function needs not be specialized as most cache implementation
    // shares the same idea.
    virtual UINT32   Find(CACHE_TAG  tag) ABSTRACT_CLASS;
    virtual VOID     Replace(CACHE_TAG& tag, CACHE_TAG &etag, ADDRINT iaddr) ABSTRACT_CLASS;
    virtual VOID     Evict(CACHE_TAG tag) ABSTRACT_CLASS;
};

/// @ CACHE_LRU_SET - brief Cache set with least recently used replacement
//  @ policy
class CACHE_LRU_SET : public CACHE_SET_BASE
{
private:
    // the use history of each cache line in the set. it is increment
    // on every access to this cache set.
    UINT16* UseStack;
public:
    // Optimization for simulation speed, cache the last block.
    UINT8   LastBlock;
private:
    BOOL Probe(UINT32 index, CACHE_TAG tag)
    {
       ++UseStack[index];
       if (CacheTags[index] == tag)
       {
           UseStack[index] = 0;
           LastBlock = index;
           return true;
       }
       return false;
    }
public:
    ///@ constructore and destructor.
    CACHE_LRU_SET(UINT32 CacheAssoc           , 
                  UINT32 CacheLevel           , 
                  CACHE_BASE* CacheBase       ) 
                  : 
                  CACHE_SET_BASE(CacheAssoc   , 
                                 CacheLevel   , 
                                 CacheBase    )
    {
        UseStack = new UINT16[CacheAssoc];
        memset(UseStack, 0, sizeof(UINT16)*CacheAssoc);
    }
    virtual ~CACHE_LRU_SET() { free (UseStack); }

    VOID Evict(CACHE_TAG tag)
    {
        INT EvictIndex = -1;
        FOREACH_CACHEWAY(if (CacheTags[index] == tag) EvictIndex = index;);
        if (EvictIndex > 0) CacheTags[EvictIndex] = 0;
    }

    UINT32 Find(CACHE_TAG tag)
    {
        // increment the access history of the cache entry.
        BOOL found = false;
        // check whether hit the last line accessed.
        if (CacheTags[LastBlock] == tag) return true;
        // did not hit into last line.
        FOREACH_CACHEWAY(found = Probe(index, tag); if (found) break;);
        return found;
    }

    VOID Replace(CACHE_TAG& tag, CACHE_TAG& etag, ADDRINT iaddr)
    {
        INT32 MaxIndex = CacheAssoc-1;
        // Is there a free entry ?
        if (CacheAssoc != (MaxIndex = GetUnused()))  goto lru_replace_entry;

        MaxIndex = 0;
        // find the entry that is least recently used. the entry that has the highest
        // UseStack is the one that is least recently used.
        FOREACH_CACHEWAY(if (UseStack[index] >= UseStack[MaxIndex]) MaxIndex = index;);
lru_replace_entry:
        etag = CacheTags[MaxIndex];
        // MaxIndex contains the entry that was least recently accessed.
        CacheTags[MaxIndex] = tag;
        UseStack[MaxIndex]  = 0;
        return;
    }
};

/// @ CacheImpl - Implemetation of the cache.
class CacheImpl
{
public:
    // Is the cache used ?
    UINT32 CacheUsed;
    // Number of sets in the cache.
    UINT32 CacheSetNum;
    // Associativity of the cache.
    UINT32 CacheAssoc;
    // Level of the cache.
    UINT32 CacheLevel;
    // Cache stats for load and store and hit and miss.
    CACHE_STATS CacheAccess[2][2];

    // The cache that owns this cache implementation.
    CACHE_BASE *CacheBase;

    // all the sets in the cache.
    // this is a base class pointer. it can be used to point to
    // subclasses of the CACHE_SET::CACHE_SET_BASE.
    CACHE_SET_BASE **CacheSets;

public:
    /// @ Evict - evict a block from this cache
    virtual VOID Evict(CACHE_TAG &tag, UINT32 setindex)
    {
        ASSERTX(setindex <= CacheSetNum);
        CacheSets[setindex]->Evict(tag); 
    }
   
    /// @ Shutdown - Shutdown the cache table in use.
    virtual void Shutdown()
    {
        for (UINT32 i=0; i<CacheSetNum; ++i) delete CacheSets[i];
        delete [] CacheSets;
    }
public:
    /// @ constructor and destructor.
    CacheImpl(UINT32 SetNum        , 
              UINT32 SetAssoc      , 
              UINT32 level         , 
              CACHE_BASE* cache    )        
              : 
              CacheUsed(0)         , 
              CacheSetNum(SetNum)  , 
              CacheAssoc(SetAssoc) , 
              CacheLevel(level)    ,
              CacheBase(cache)       
    {
        ASSERTX(CacheSetNum);
        for (INT32 type=0; type<2; ++type)
        {
            CacheAccess[type][true]  = 0;
            CacheAccess[type][false] = 0;
        }

        /// ------------------------------------------------ ///
        //  Cache set initialization                          //
        /// ------------------------------------------------ ///
        // CacheSets is an array of cache set pointers.
        CacheSets = new CACHE_SET_BASE*[CacheSetNum];
        memset(CacheSets, 90, sizeof(CACHE_SET_BASE*)*CacheSetNum);
        for (UINT32 i=0; i<CacheSetNum; i++)
        {
            CacheSets[i] = new CACHE_LRU_SET(CacheAssoc, 
                                             CacheLevel, 
                                             CacheBase);
        }
        return;
    }
    virtual ~CacheImpl() { Shutdown(); }
};

/// @ CACHE_BASE - brief Generic cache base class; no allocate specialization,
//  @ no cache set specialization. This is the base class of all caches,
class CACHE_BASE
{
public:
// type of cache.
typedef enum
{
  CACHE_TYPE_ICACHE,
  CACHE_TYPE_DCACHE,
  CACHE_TYPE_NUM
} CACHE_TYPE;
// do we allocate and evict if there is a store miss ?
typedef enum
{
  CACHE_STORE_NO_ALLOCATE,
  CACHE_STORE_ALLOCATE
} CACHE_STORE;
// types of accesses.
typedef enum
{
  ACCESS_TYPE_LOAD,
  ACCESS_TYPE_STORE,
  ACCESS_TYPE_NUM
} ACCESS_TYPE;
public:
    std::list<PageRecord> pages;
protected:
    // the name of the cache.
    string CacheName;
    // the type of the set of the cache.
    UINT32 CacheSetType;
    // level of the cache.
    INT32  CacheLevel;
    // the maximum number of sets in the cache.
    UINT32 CacheMaxSets;
    // whether to allocate on store misses ?
    UINT32 CacheStoreAlloc;
    // Cache parameters.
    const UINT32 CacheSize;
    const UINT32 CacheLineSize;
    const UINT32 CacheAssoc;
    // Cache computed params
    const UINT32 CacheLineShift;
    const UINT32 CacheSetIndexMask;

private:
    // private cache or not.
    BOOL IsPrivate() const   { return CacheLevel < 3; }
    // shutdown the cache and free the resources.
    VOID Shutdown()
    {
      if (CACHESIM_likely(IsPrivate())) { FOREACH_CACHE(GetCache(index)->Shutdown()); }
      else ShrdCache->Shutdown();
    }
public:
    // The only physical manifestation of the cache. Used for LLC.
    CacheImpl* ShrdCache;
    // The per thread physical manifestation of the cache. Used for private cache.
    CacheImpl* PrivCache[MAX_CACHE_THREAD];

    // constructors/destructors
    CACHE_BASE(std::string name     , 
               UINT32 level         , 
               UINT32 size          , 
               UINT32 lsize         , 
               UINT32 assoc         , 
               std::string type     , 
               UINT32 storealloc    , 
               OS_THREAD_ID threadid)
               : 
               CacheName(name)                   ,
               CacheLevel(level)                 ,
               CacheMaxSets(size/(lsize*assoc))  ,
               CacheStoreAlloc(storealloc)       ,
               CacheSize(size)                   ,
               CacheLineSize(lsize)              ,
               CacheAssoc(assoc)                 ,
               CacheLineShift(FloorLog2(lsize))  ,
               CacheSetIndexMask((size/(assoc*lsize))-1)
     {
        ASSERTX(CacheMaxSets);
        ASSERTX(IsPowerOfTwo(CacheLineSize));
        ASSERTX(IsPowerOfTwo(CacheSetIndexMask + 1));

        if (IsPrivate())
        {
            FOREACH_CACHE(PrivCache[index]=new CacheImpl(CacheMaxSets, 
                                                         CacheAssoc, 
                                                         CacheLevel, this));
        }
        else ShrdCache = new CacheImpl(CacheMaxSets, 
                                       CacheAssoc, 
                                       CacheLevel, this);
        return;
    }
    virtual ~CACHE_BASE() { Shutdown(); }

    /// Return cache parameters.
    UINT32 GetCacheSize()     const { return CacheSize;        }
    UINT32 GetLineSize()      const { return CacheLineSize;    }
    UINT32 GetMaxSets()       const { return CacheMaxSets;     }
    UINT32 GetStoreAlloc()    const { return CacheStoreAlloc;  }
    UINT32 GetAssociativity() const { return CacheAssoc;       }

    // accessors
    CacheImpl *GetCache(THREADID tid) const
    {
        CacheImpl *Cache=PrivCache[tid];
        if (CACHESIM_unlikely(!IsPrivate())) Cache=ShrdCache;
        Cache->CacheUsed=1;
        return Cache;
    }
    VOID SplitAddress(const ADDRINT addr, UINT32& setindex) const
    {
        CACHE_TAG tag = addr >> CacheLineShift;
        setindex = tag & CacheSetIndexMask;
    }

    VOID SplitAddress(const ADDRINT addr, CACHE_TAG& tag, UINT32& setindex) const
    {
        tag = addr >> CacheLineShift;
        setindex = tag & CacheSetIndexMask;
    }

    VOID SplitAddress(const ADDRINT addr, CACHE_TAG& tag, UINT32& setIndex, UINT32& lineIndex) const
    {
        const UINT32 lineMask = CacheLineSize - 1;
        lineIndex = addr & lineMask;
        SplitAddress(addr, tag, setIndex);
    }


    // Stats Reporting Functions.
    CACHE_STATS Hits(THREADID tid)                       const { return SumAccess(true, tid);                                        }
    CACHE_STATS Misses(THREADID tid)                     const { return SumAccess(false, tid);                                       }
    CACHE_STATS Accesses(THREADID tid)                   const { return Hits(tid) + Misses(tid);                                     }
    CACHE_STATS Hits(ACCESS_TYPE type, THREADID tid)     const { return GetCache(tid)->CacheAccess[type][true];                      }
    CACHE_STATS Misses(ACCESS_TYPE type, THREADID tid)   const { return GetCache(tid)->CacheAccess[type][false];                     }
    CACHE_STATS Accesses(ACCESS_TYPE type, THREADID tid) const { return Hits(type, tid) + Misses(type, tid);                         }
    CACHE_STATS HitsAll(ACCESS_TYPE type)                const { FOREACH_CACHE_SUM(GetCache(index)->CacheAccess[type][true]);        }
    CACHE_STATS MissesAll(ACCESS_TYPE accessType)        const { FOREACH_CACHE_SUM(GetCache(index)->CacheAccess[accessType][false]); }
    CACHE_STATS HitsAll()                                const { FOREACH_CACHE_SUM(SumAccess(true, index));                          }
    CACHE_STATS MissesAll()                              const { FOREACH_CACHE_SUM(SumAccess(false, index));                         }
    CACHE_STATS AccessesAll(ACCESS_TYPE type)            const { return HitsAll(type) + MissesAll(type);                             }
    CACHE_STATS AccessesAll()                            const { return HitsAll() + MissesAll();                                     }
    CACHE_STATS SumAccess(BOOL hit, THREADID tid)        const {  FOREACH_CACHEACCESS_SUM(GetCache(tid)->CacheAccess[index][hit];);  }

    // Return the parameterics of the cache.
    string StatsParam(void) const
    {
       const UINT32 numberWidth = 12;
       string out;
       out += "# " + CacheName + "\n";
       out += "# Cache Total Size : " + mydecstr(CacheSize, numberWidth) + "\n";
       out += "# Cache Line Size : "  +  mydecstr(CacheLineSize, numberWidth) + "\n";
       out += "# Cache Associativity : " +  mydecstr(CacheAssoc, numberWidth/2) + "\n";
       return out;
    }

    string StatsLong(string prefix = "", CACHE_TYPE = CACHE_TYPE_DCACHE, THREADID tid = MAX_CACHE_THREAD) const;
    string StatsLongAll(string prefix = "", CACHE_TYPE = CACHE_TYPE_DCACHE);
};


string CACHE_BASE::StatsLong(string prefix, CACHE_TYPE cache_type, THREADID tid) const
{
    string out;

    // The cache has never been used.
    if (!GetCache(tid)->CacheUsed) return out;

    const UINT32 headerWidth = 19;
    const UINT32 numberWidth = 12;

    out += prefix +  ":" + "\n";

    if (cache_type != CACHE_TYPE_ICACHE)
    {
        // there are read and write accesses for data cache.
        for (UINT32 i = 0; i < ACCESS_TYPE_NUM; i++)
        {
            const ACCESS_TYPE accessType = ACCESS_TYPE(i);

            std::string type(accessType == ACCESS_TYPE_LOAD ? "Load" : "Store");

            out += prefix + ljstr(type + "-Hits:      ", headerWidth)
                   + mydecstr(Hits(accessType, tid), numberWidth)  +
                   "  " +fltstr(100.0 * Hits(accessType, tid) / Accesses(accessType, tid), 2, 6) + "%\n";

            out += prefix + ljstr(type + "-Misses:    ", headerWidth)
                   + mydecstr(Misses(accessType, tid), numberWidth) +
                   "  " +fltstr(100.0 * Misses(accessType, tid) / Accesses(accessType, tid), 2, 6) + "%\n";

            out += prefix + ljstr(type + "-Accesses:  ", headerWidth)
                   + mydecstr(Accesses(accessType, tid), numberWidth) +
                   "  " +fltstr(100.0 * Accesses(accessType, tid) / Accesses(accessType, tid), 2, 6) + "%\n";

            out += prefix + "\n";
        }
    }

    // there is only read access for instruction cache.
    out += prefix + ljstr("Total-Hits:      ", headerWidth)
           + mydecstr(Hits(tid), numberWidth) +
           "  " +fltstr(100.0 * Hits(tid) / Accesses(tid), 2, 6) + "%\n";

    out += prefix + ljstr("Total-Misses:    ", headerWidth)
           + mydecstr(Misses(tid), numberWidth) +
           "  " +fltstr(100.0 * Misses(tid) / Accesses(tid), 2, 6) + "%\n";

    out += prefix + ljstr("Total-Accesses:  ", headerWidth)
           + mydecstr(Accesses(tid), numberWidth) +
           "  " +fltstr(100.0 * Accesses(tid) / Accesses(tid), 2, 6) + "%\n";

    out += prefix + ljstr("Total MPKI:  ", headerWidth)
           + "  " +fltstr(1000.0 * Misses(tid) / simglobals->get_global_icount(), 2, 6) + "\n";

    out += "\n";

    return out;
}

string CACHE_BASE::StatsLongAll(string prefix, CACHE_TYPE cache_type)
{
    string out;

    const UINT32 headerWidth = 19;
    const UINT32 numberWidth = 12;

    out += prefix +  ":" + "\n";

    if (cache_type != CACHE_TYPE_ICACHE)
    {
        // there are read and write accesses for data cache.
        for (UINT32 i = 0; i < ACCESS_TYPE_NUM; i++)
        {
            const ACCESS_TYPE accessType = ACCESS_TYPE(i);

            std::string type(accessType == ACCESS_TYPE_LOAD ? "Load" : "Store");

            out += prefix + ljstr(type + "-Hits:      ", headerWidth)
                   + mydecstr(HitsAll(accessType), numberWidth)  +
                   "  " +fltstr(100.0 * HitsAll(accessType) / AccessesAll(accessType), 2, 6) + "%\n";

            out += prefix + ljstr(type + "-Misses:    ", headerWidth)
                   + mydecstr(MissesAll(accessType), numberWidth) +
                   "  " +fltstr(100.0 * MissesAll(accessType) / AccessesAll(accessType), 2, 6) + "%\n";

            out += prefix + ljstr(type + "-Accesses:  ", headerWidth)
                   + mydecstr(AccessesAll(accessType), numberWidth) +
                   "  " +fltstr(100.0 * AccessesAll(accessType) / AccessesAll(accessType), 2, 6) + "%\n";

            out += prefix + "\n";
        }
    }

    // there is only read access for instruction cache.
    out += prefix + ljstr("Total-Hits:      ", headerWidth)
           + mydecstr(HitsAll(), numberWidth) +
           "  " +fltstr(100.0 * HitsAll() / AccessesAll(), 2, 6) + "%\n";

    out += prefix + ljstr("Total-Misses:    ", headerWidth)
           + mydecstr(MissesAll(), numberWidth) +
           "  " +fltstr(100.0 * MissesAll() / AccessesAll(), 2, 6) + "%\n";

    out += prefix + ljstr("Total-Accesses:  ", headerWidth)
           + mydecstr(AccessesAll(), numberWidth) +
           "  " +fltstr(100.0 * AccessesAll() / AccessesAll(), 2, 6) + "%\n";

    out += prefix + ljstr("Total MPKI:  ", headerWidth)
           + "  " +fltstr(1000.0 * MissesAll() / simglobals->get_global_icount(), 2, 6) + "\n";


#if 0
    /// sort by install count.
    pages.sort(SortByAccessCount);

    std::list<PageRecord>::iterator I=pages.begin();
    std::list<PageRecord>::iterator E=pages.end();

    out += "# \n";
    out += "# Detailed TLB stats sort by access count\n";
    out += "# \n";
    out += "# total " + StringInt(pages.size()) + " pages\n";
    out += "# \n";
    out += "# tag access_count	refill_count	access_per_refill	%_of_refills\n";
    for (; I!=E; ++I)
    {
        out += (*I).PageStats(MissesAll());
        out += (*I).BlockAccessStats();
        out += (*I).BlockMissStats();
    }
    out += "# \n";


    pages.sort(SortByRefillCount);

    I=pages.begin();
    E=pages.end();

    out += "# \n";
    out += "# Detailed TLB stats sort by refill count\n";
    out += "# \n";
    out += "# total " + StringInt(pages.size()) + " pages\n";
    out += "# \n";
    out += "# tag access_count	refill_count	access_per_refill	%_of_refills\n";
    for (; I!=E; ++I)
    {
        out += (*I).PageStats(MissesAll());
        out += (*I).BlockAccessStats();
        out += (*I).BlockMissStats();
    }
#endif
    out += "# \n";

    return out;
}

/// @ CACHE - brief cache class with specific cache set allocation policies
//  @ All that remains to be done here is allocate and deallocate the right
//  @ type of cache sets.
class CACHE : public CACHE_BASE
{
public:
   // higher and lower level cache.
   std::set<CACHE*> prev;
public:
    // constructors/destructors
    CACHE(std::string name    ,      // name of the cache. 
          UINT32 level        ,      // level of the cache.
          UINT32 size         ,      // total size of the cache.
          UINT32 linesize     ,      // block size of the cache.
          UINT32 associativity,      // associativity of the cache.
          std::string rep     ,      // replacement policy.
          UINT32 storealloc   ,      // allocation on store. 
          THREADID threadid   )
          :  
          CACHE_BASE(name         , 
                     level        , 
                     size         , 
                     linesize     , 
                     associativity, 
                     rep          , 
                     storealloc   , 
                     threadid     ) 
    {
       assert(IsPowerOfTwo(size));
       assert(IsPowerOfTwo(linesize));
       prev.clear();
    }

    /// Cache access from addr to addr+size-1
    BOOL Access(ADDRINT iaddr, ADDRINT addr, UINT32 size, ACCESS_TYPE type, THREADID tid);
    /// Cache access at addr that does not span cache lines
    BOOL AccessSingleLine(ADDRINT iaddr, ADDRINT addr, ACCESS_TYPE type, THREADID tid);
    BOOL AccessPage(ADDRINT addr, ACCESS_TYPE type, THREADID tid);

    /// set up the higher lower and higher level cache.
    VOID SetPrev(CACHE *cache) { if (cache) prev.insert(cache); }
    VOID Evict(ADDRINT addr, THREADID tid)
    {
        CACHE_TAG tag=0;
        UINT32 setindex=0;
        SplitAddress(addr, tag, setindex);
        GetCache(tid)->Evict(tag, setindex);
        EvictPrev(addr, tid);
    }
    VOID EvictPrev(ADDRINT addr, THREADID tid)
    {
       for(std::set<CACHE*>::iterator I=prev.begin(), E=prev.end(); I!=E; ++I) (*I)->Evict(addr, tid);
    }

    /// return the page.
    PageRecord* GetPageRecord(CACHE_TAG tag)
    {
refind:
        std::list<PageRecord>::iterator I=pages.begin();
        std::list<PageRecord>::iterator E=pages.end();

        for (; I!=E; ++I)
        {
            if ((*I).tag == tag) return &(*I);
        }

        if (I==E) pages.push_back(PageRecord(tag));

        goto refind;
    }
};

/// @ implements a coherence directory.
class COHERENCE 
{
private:
   std::map<ADDRINT, UINT64> ownerlist;
public:
   COHERENCE() { ownerlist.clear(); }
   virtual ~COHERENCE() { /* do nothing here */ }
   
   /// Add and remove 
   VOID AddOwner(ADDRINT addr, THREADID tid) { ownerlist[addr] |=  (1<<tid); }
   VOID SubOwner(ADDRINT addr, THREADID tid) { ownerlist[addr] &= ~(1<<tid); }
   BOOL SingleOwner(ADDRINT addr)            { return IsPowerOfTwo(ownerlist[addr]); } 
   UINT64 ReturnOwners(ADDRINT addr)         { return ownerlist[addr];       }

public:
   std::string StatsLong() const 
   {
        const UINT32 numberWidth = 12;

        std::string out;
        unsigned private_pages = 0;
        std::map<ADDRINT, UINT64>::const_iterator I;
        std::map<ADDRINT, UINT64>::const_iterator E;
        for (I=ownerlist.begin(), E=ownerlist.end(); I!=E; ++I) private_pages+= IsPowerOfTwo((*I).second);
        out += "# There are " + mydecstr(private_pages, numberWidth) + " private pages out of " 
            + mydecstr(ownerlist.size(), numberWidth) + " pages\n";
        return out;
   }
};

#endif // PIN_CACHESIM_H
