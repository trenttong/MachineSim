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

/// @ --------------------------------------------------------------------------
//  @
//  @ There are a few parameters that can be varied for instruction caches
//  @
//  @ They are implemented to facilitate fast simulation setup.
//  @
//  @ 1. cache size
//  @  - L1 instruction cache size in the state of the art processor 
//  @    ranges from 32 - 64 KB.
//  @
//  @ 2. cache associativity 
//  @  - direct-mapped    : associativity == 1.
//  @  - set-associative  : associativity == n.
//  @  - fully-associative: associativity == cache size.
//  @
//  @ 3. cache replacement policy
//  @  - least recently used   : replace the cache line that was accessed 
//  @                            least recenlty
//  @  - least frequently used : removes entities from the cache that are 
//  @                            used the least. 
//  @  - random replacement    : randomly select a candidate and replace.
//  @  - round robin           : replace the cache in a round robin fashion.
//  @  - etc
//  @
//  @ In this tool, to initialize a cache, the following parameters need 
//  @ to be given
//  @
//  @ - std::string name  : name of the cache.
//  @ - UINT32 cacheLevel : level of the cache.
//  @ - UINT32 cacheSize  : size of the cache.
//  @ - UINT32 lineSize   : line size of the cache.
//  @ - UINT32 associativity : associativity of the set.
//  @ - UINT32 setType : type of the set.
//  @ - UINT32 storeAlloc : whether to allocate on store miss.
//  @
//  @ CMP Simulation
//  @
//  @ To support cache simulation for CMP system. Every level 1 and 2 caches are
//  @ tagged with PID as they are private to a processor core (and the thread 
//  @ running on the core). 
//  @ Tagging every private cache with a PID gives accurate simulation results
//  @ for every thread.
//  @ Lets say there are 4 threads in the process of interest. If there is only
//  @ 2 cores, the OS will schedule the 4 threads on the 2 cores. then the system
//  @ this tool will simulate is a 2 core system. i.e. the LLC cache being 2M*number
//  @ of cores the underlying machine has. 
//  @ however, the private cache should still be simulated on a per thread basis
//  @ as when thread is switched in, it and only it has access to the private
//  @ cache on the core it is executing.
/// @ --------------------------------------------------------------------------


#ifndef PIN_CACHE_H
#define PIN_CACHE_H

extern UINT64 GlobalL1DForceEvictCount; 
extern UINT64 GlobalL2ForceEvictCount; 
extern UINT64 GlobalL3ForceEvictCount;
extern UINT64 GlobalL1ICacheReplacementCount;
extern UINT64 GlobalL1DCacheReplacementCount;
extern UINT64 GlobalL2CacheReplacementCount;
extern UINT64 GlobalL3CacheReplacementCount;
extern UINT64 GlobalL1ICacheReplacementLength;
extern UINT64 GlobalL1DCacheReplacementLength;
extern UINT64 GlobalL2CacheReplacementLength;
extern UINT64 GlobalL3CacheReplacementLength;
extern UINT64 GlobalInstCount;

#define KILO 1024
#define MEGA (KILO*KILO)
#define GIGA (KILO*MEGA)
#define ABSTRACT_CLASS =0
#define MAX_CACHE_THREAD 1024 

typedef UINT64 CACHE_STATS; // type of cache hit/miss counters

#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <string>

#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>

/* ===================================================================== */
/* General Utilities */
/* ===================================================================== */

static string mydecstr(UINT64 v, UINT32 w)
{
  ostringstream o;
  o.width(w);
  o << v;
  string str(o.str());
  return str;
}

static inline bool IsPower2(UINT32 n)
{
  return ((n & (n - 1)) == 0);
}

static inline INT32 FloorLog2(UINT32 n)
{
  INT32 p = 0;

  if (n == 0) return -1;

  if (n & 0xffff0000) { p += 16; n >>= 16; }
  if (n & 0x0000ff00)	{ p +=  8; n >>=  8; }
  if (n & 0x000000f0) { p +=  4; n >>=  4; }
  if (n & 0x0000000c) { p +=  2; n >>=  2; }
  if (n & 0x00000002) { p +=  1; }

  return p;
}

static inline INT32 CeilLog2(UINT32 n)
{
  return FloorLog2(n - 1) + 1;
}


/// @ CACHE_TAG - this class implements a tag in the cache.
class CACHE_TAG
{
private:
  ADDRINT CacheTag;

public:
  CACHE_TAG(ADDRINT tag = 0) { CacheTag = tag; }
  bool operator==(const CACHE_TAG &right) const { return CacheTag == right.CacheTag; }
  operator ADDRINT() const { return CacheTag; }
};


/// @ CACHE_SET - Everything related to cache sets
//  @
//  @ To specify a cache set. Parameters which need to be specified are listed below.
//  @
//  @ - associativity - how many lines are in the set
//  @ - replacement   - what is the replacement policy for the set.
namespace CACHE_SET
{

enum 
{
  CACHE_SET_BASE_SETTYPE,
  RR_SETTYPE,
  LRU_SETTYPE,
  LFU_SETTYPE,
  SHIP_SETTYPE
};


/* ===================================================================== */
/* Cache Sets ... */
/* ===================================================================== */

/// @ CACHE_SET_BASE - This is the base of all cache sets. To add a new 
//  @ cache size, follow these steps.
//  @
//  @  1. Add a subclass of CACHE_SET_BASE.
//  @  2. Add a class type in the enum SET_TYPE.
//  @  3. Add a check in class CACHE constructor.
class CACHE_SET_BASE
{
protected:
  // associativity of the cache size, this determines the number of 
  // cache lines a cache set holds.
  INT32 CacheAssociativity;
  // level of cache.
  INT32 CacheLevel;
  // all the tags in the cache size.
  CACHE_TAG *CacheTags;
  // Lines in the set in use ?.
  INT32 *CacheStatus;

public:
  /// @ constructor.
  CACHE_SET_BASE(UINT32 associativity, INT32 CacheLevel) : CacheAssociativity(associativity),
                                                           CacheLevel(CacheLevel),  
							   CacheTags(0), CacheStatus(0)
  { 
     // allocate the tags here.
     CacheTags   = new CACHE_TAG[CacheAssociativity]; 
     CacheStatus = new INT32[CacheAssociativity];

     for(int cacheIdx = 0; cacheIdx < CacheAssociativity; cacheIdx++)
     {
        CacheStatus[cacheIdx] = 0;
     }
  }

  virtual VOID   SetAssociativity(UINT32 associativity) { CacheAssociativity = associativity; }
  virtual UINT32 GetAssociativity(UINT32 associativity) { return CacheAssociativity; }

  /// @ GetFree - get the free entry in the cache set.
  //  @
  //  @ paramaters - None.
  //  @
  //  @ side effects - None.
  //  @
  //  @ return - the entry that is free. return CacheAssociativity if
  //  @ no entry is free.
  INT32 GetFree(void)
  {
    for(int cacheIdx = 0; cacheIdx < CacheAssociativity; cacheIdx++)
    {
       if (CacheStatus[cacheIdx] == 0) return cacheIdx;
    }
    return CacheAssociativity; 
  }

  // Find and Replace are the only two functions that need to be specialized.
  // Usually the Find function needs not be specialized as most cache implementation
  // shares the same idea.
  virtual UINT32 Find(CACHE_TAG  tag) ABSTRACT_CLASS;
  virtual VOID Free(CACHE_TAG tag, ADDRINT iaddr) ABSTRACT_CLASS; 
  virtual VOID Replace(CACHE_TAG tag, ADDRINT iaddr) ABSTRACT_CLASS; 
  virtual VOID ReplaceStats(INT32 OneToReplace) ABSTRACT_CLASS;
};

/// @ LRU_SET - brief Cache set with least recently used replacement 
//  @ policy
class LRU_SET : public CACHE_SET_BASE
{
private:
  // the use history of each cache line in the set. it is increment
  // on every access to this cache set.
  UINT32* UseHistory;
  // the instruction timestamp when the cache line is brought in.
  UINT64* InstStamp;

public:
  LRU_SET(UINT32 associativity, INT32 CacheLevel) : CACHE_SET_BASE(associativity, CacheLevel)
  {
    UseHistory = new UINT32[associativity];
    InstStamp = new UINT64[associativity];
    memset(UseHistory, 0, sizeof(UINT32)*associativity);
    memset(InstStamp, 0, sizeof(UINT64)*associativity);
  }

  virtual UINT32 Find(CACHE_TAG tag) 
  {  
    // increment the access history of the cache entry.
    for(int cacheIdx = 0; cacheIdx < CacheAssociativity; cacheIdx++)
    {
       UseHistory[cacheIdx] ++;
    }

    for(int cacheIdx = 0; cacheIdx < CacheAssociativity; cacheIdx++)
    {
       // found a matching line. update the access recency of the line.
       if (CacheTags[cacheIdx] == tag)
       {
          UseHistory[cacheIdx] = 0;
          return true;
       }
    }
    return false;
  }

  virtual VOID Free(CACHE_TAG tag, ADDRINT iaddr) 
  {
    INT32 currIndex = CacheAssociativity - 1;
    for(;currIndex >= 0; currIndex--)
    {
       if (CacheTags[currIndex] == tag) 
       {
	  UseHistory[currIndex]=1024*1024*1024;
          if (CacheLevel==1) GlobalL1DForceEvictCount ++; 
	  if (CacheLevel==2) GlobalL2ForceEvictCount  ++; 
          if (CacheLevel==3) GlobalL3ForceEvictCount  ++; 
       }
    } 
  }

  virtual VOID Replace(CACHE_TAG tag, ADDRINT iaddr)
  {
    INT32 OneToReplace = 0; 
    INT32 currIndex = CacheAssociativity - 1;
    INT32 maxIndex = currIndex; 
    
    // Is there a free entry ?
    if (CacheAssociativity != (OneToReplace = GetFree()))  goto lru_replace_entry;

    // find the entry that is least recently used. the entry that has the highest
    // UseHistory is the one that is least recently used.
    for(;currIndex >= 0; currIndex--)
    {
       if (UseHistory[currIndex] >= UseHistory[maxIndex]) maxIndex = currIndex;
    } 
    OneToReplace = maxIndex;
    
lru_replace_entry:

    ReplaceStats(OneToReplace);
 
    // maxIndex contains the entry that was least recently accessed.
    // replace it.
    CacheStatus[OneToReplace] = 1;
    CacheTags[OneToReplace] = tag;
    UseHistory[OneToReplace] = 0;
    InstStamp[OneToReplace] = GlobalInstCount;
  }

  virtual VOID ReplaceStats(INT32 OneToReplace)
  {
    // Record the cache block life time.
    if (CacheLevel == 3)
    {
      GlobalL3CacheReplacementCount ++;
      GlobalL3CacheReplacementLength += GlobalInstCount-InstStamp[OneToReplace];
    }
    else if (CacheLevel == 2)
    {
      GlobalL2CacheReplacementCount ++;
      GlobalL2CacheReplacementLength += GlobalInstCount-InstStamp[OneToReplace];
    }
    else if (CacheLevel == 1)
    {
      GlobalL1DCacheReplacementCount ++;
      GlobalL1DCacheReplacementLength += GlobalInstCount-InstStamp[OneToReplace];
    }
    else 
    {
      // Unknown level ?.
      ASSERTX(0);
    }
  }
};
} // namespace CACHE_SET

/// @ CacheImpl - Implemetation of the cache.
class CacheImpl 
{
public:
  // Is the cache used ?
  UINT32 CacheUsed;

  // Number of sets in the cache.
  UINT32 CacheSetNum;

  // Associativity of the cache.
  UINT32 CacheAssociativity;

  // Level of the cache.
  INT32 CacheLevel;

  // Cache stats for load and store and hit and miss.
  CACHE_STATS CacheAccess[2][2];

  // all the sets in the cache.
  // this is a base class pointer. it can be used to point to
  // subclasses of the CACHE_SET::CACHE_SET_BASE.
  CACHE_SET::CACHE_SET_BASE **CacheSets;

public:
   CacheImpl(UINT32 SetNum, UINT32 SetAssoc, INT32 level) : CacheUsed(0), CacheSetNum(SetNum), CacheAssociativity(SetAssoc), CacheLevel(level) 
   {
      for(UINT32 accessType = 0; accessType < 2; accessType++)
      {
         CacheAccess[accessType][false] = 0;
         CacheAccess[accessType][true] = 0;
      }

      /// ------------------------------------------------ ///
      //  Cache set initialization                          //
      /// ------------------------------------------------ ///
      // CacheSets is an array of cache set pointers.
      CacheSets = new CACHE_SET::CACHE_SET_BASE*[CacheSetNum];
      for(UINT32 i = 0; i < CacheSetNum; i++)
      {
         CacheSets[i] = new CACHE_SET::LRU_SET(CacheAssociativity, CacheLevel);
         ASSERTX(CacheSets[i]);
      }
    }

public:
  /// @ Shutdown - Shutdown the cache table in use.
  //  @
  //  @ paramaters - None.
  //  @
  //  @ side effects - The cache table freed.
  //  @
  //  @ return - None.
  virtual void Shutdown() 
  {
    for(UINT32 i = 0; i < CacheSetNum; i++)
    {
       delete (CacheSets[i]);
    }
    delete [] CacheSets;
    return;
  }
};

/// @ CACHE_BASE - brief Generic cache base class; no allocate specialization, 
//  @ no cache set specialization. This is the base class of all caches, 
//  @ some of the important cache parameters are listed below.
//  @
//  @ - CacheSize - the total size of the cache
//  @ - CacheLineSize  - the line size of the cache
//  @ - CacheAssociativity - the associativity of the cache
//  @ - CacheConfig - the type of the cache. i.e. directly-mapped, etc.
class CACHE_BASE
{
public:
  // types, constants
  typedef enum 
  {
    ACCESS_TYPE_LOAD,
    ACCESS_TYPE_STORE,
    ACCESS_TYPE_NUM
  } ACCESS_TYPE;

  typedef enum
  {
    CACHE_TYPE_ICACHE,
    CACHE_TYPE_DCACHE,
    CACHE_TYPE_NUM
  } CACHE_TYPE;

  enum
  {
    CACHE_DIRECT_MAPPED,
    CACHE_SET_ASSOCIATIVE,
    CACHE_FULLY_ASSOCIATIVE
  };

  // do we allocate and evict if there is a store miss ?
  enum 
  {
    STORE_ALLOCATE,
    STORE_NO_ALLOCATE
  };

protected:
  // the name of the cache.
  std::string CacheName;
  // the type of the set of the cache.
  UINT32 CacheSetType;
  // level of the cache.
  INT32  CacheLevel;
  // the maximum number of sets in the cache.
  UINT32 CacheMaxSets; 

  // whether to allocate on store misses ?
  UINT32 CacheStoreAlloc;

  const UINT32 CacheSize;
  const UINT32 CacheLineSize;
  const UINT32 CacheAssociativity;
  UINT32 CacheConfig;

  // computed params
  const UINT32 CacheLineShift;
  const UINT32 CacheSetIndexMask;

  CACHE_STATS SumAccess(bool hit, THREADID tid) const
  {
    CACHE_STATS sum = 0;
    for (UINT32 accessType = 0; accessType < ACCESS_TYPE_NUM; accessType++)
    {
       sum += GetCache(tid)->CacheAccess[accessType][hit];
    }
    return sum;
  }

protected:
  UINT32 NumSets() const { return CacheSetIndexMask + 1; }

public:
  // The only physical manifestation of the cache. Used for LLC.
  CacheImpl* ShrdCache;

  // The per thread physical manifestation of the cache. Used for private cache.
  CacheImpl *PrivCache; 

  // constructors/destructors
  CACHE_BASE(std::string name, INT32 level, UINT32 cacheSize, UINT32 lineSize, UINT32 associativity, std::string settype, UINT32 storealloc, OS_THREAD_ID threadid);

  // private cache or not.
  BOOL IsPrivate() const { return CacheLevel < 3;}

  // accessors
  CacheImpl *GetCache(THREADID tid) const 
  {
     CacheImpl *Cache=0;
     if (IsPrivate())
     {
        Cache=&PrivCache[tid];
     }
     else Cache=ShrdCache;

     Cache->CacheUsed=1;
     return Cache;
  }

  BOOL GetUsed(THREADID tid) const
  {
     if (IsPrivate())
     {
       return (&PrivCache[tid])->CacheUsed;
     }
     return ShrdCache->CacheUsed;
  }

  UINT32 GetCacheSize() const { return CacheSize; }
  UINT32 GetLineSize() const { return CacheLineSize; }
  UINT32 GetMaxSets() const { return CacheMaxSets; }
  UINT32 GetStoreAlloc() const { return CacheStoreAlloc; }
  UINT32 GetAssociativity() const { return CacheAssociativity; }
  UINT32 GetCacheConfig() const { return CacheConfig; }

  // Stats Reporting Functions.
  CACHE_STATS Hits(ACCESS_TYPE accessType, THREADID tid) const { return GetCache(tid)->CacheAccess[accessType][true];}
  CACHE_STATS Misses(ACCESS_TYPE accessType, THREADID tid) const { return GetCache(tid)->CacheAccess[accessType][false];}
  CACHE_STATS Accesses(ACCESS_TYPE accessType, THREADID tid) const { return Hits(accessType, tid) + Misses(accessType, tid);}
  CACHE_STATS Hits(THREADID tid) const { return SumAccess(true, tid);}
  CACHE_STATS Misses(THREADID tid) const { return SumAccess(false, tid);}
  CACHE_STATS Accesses(THREADID tid) const { return Hits(tid) + Misses(tid);}

  CACHE_STATS HitsAll(ACCESS_TYPE accessType) const 
  { 
     CACHE_STATS Sum=0;
     UINT32 index=0;
     for(index=0;index<MAX_CACHE_THREAD;index++)
     {
        Sum+=GetCache(index)->CacheAccess[accessType][true];
     }
     return Sum;
  }
  CACHE_STATS MissesAll(ACCESS_TYPE accessType) const 
  { 
     CACHE_STATS Sum=0;
     UINT32 index=0;
     for(index=0;index<MAX_CACHE_THREAD;index++)
     {
        Sum+=GetCache(index)->CacheAccess[accessType][false];
     }
     return Sum;
  }
  CACHE_STATS HitsAll() const 
  { 
     CACHE_STATS Sum=0;
     UINT32 index=0;
     for(index=0;index<MAX_CACHE_THREAD;index++)
     {
        Sum+=SumAccess(true, index);
     }
     return Sum;
  }
  CACHE_STATS MissesAll() const
  { 
     CACHE_STATS Sum=0;
     UINT32 index=0;
     for(index=0;index<MAX_CACHE_THREAD;index++)
     {
        Sum+=SumAccess(false, index);
     }
     return Sum;
  }
  CACHE_STATS AccessesAll(ACCESS_TYPE accessType) const { return HitsAll(accessType) + MissesAll(accessType);}
  CACHE_STATS AccessesAll() const { return HitsAll() + MissesAll();}
  

  VOID SplitAddress(const ADDRINT addr, CACHE_TAG & tag, UINT32 & setindex) const
  {
    tag = addr >> CacheLineShift;
    setindex = tag & CacheSetIndexMask;
  }

  VOID SplitAddress(const ADDRINT addr, CACHE_TAG & tag, UINT32 & setIndex, UINT32 & lineIndex) const
  {
    const UINT32 lineMask = CacheLineSize - 1;
    lineIndex = addr & lineMask;
    SplitAddress(addr, tag, setIndex);
  }

  VOID Shutdown() 
  { 
    if (IsPrivate())
    {
      int index=0;
      for(index=0;index<MAX_CACHE_THREAD;index++) GetCache(index)->Shutdown(); 
    }
    else ShrdCache->Shutdown();
  }

  // Return the parameterics of the cache.
  string StatsParam(void) const;
  string StatsLong(string prefix = "", CACHE_TYPE = CACHE_TYPE_DCACHE, THREADID tid = MAX_CACHE_THREAD) const;
  string StatsLongAll(string prefix = "", CACHE_TYPE = CACHE_TYPE_DCACHE) const;

  // The theoretical eviction cycle of an unused cache line.
  UINT64 GetTheorectialEvictionCycles() const;
};

CACHE_BASE::CACHE_BASE(std::string name, INT32 level, UINT32 cacheSize, UINT32 lineSize, UINT32 associativity, std::string SetTypeString, UINT32 storeAlloc, OS_THREAD_ID threadid)
  : CacheName(name),
    CacheLevel(level),
    CacheMaxSets(cacheSize/(lineSize*associativity)),
    CacheStoreAlloc(storeAlloc),
    CacheSize(cacheSize),
    CacheLineSize(lineSize),
    CacheAssociativity(associativity),
    CacheLineShift(FloorLog2(lineSize)),
    CacheSetIndexMask((cacheSize / (associativity * lineSize)) - 1)
{
  ASSERTX(IsPower2(CacheLineSize));
  ASSERTX(IsPower2(CacheSetIndexMask + 1));
  
  if (IsPrivate())
  {
    // Level 1 and 2 caches are typically small. instantiate them here.
    PrivCache = new CacheImpl[MAX_CACHE_THREAD](CacheMaxSets, CacheAssociativity, level);
  }
  else 
  {
    // Level 3 cache is not PID specific.
    ShrdCache = new CacheImpl(CacheMaxSets, CacheAssociativity, level);
  }
}

string CACHE_BASE::StatsParam(void) const
{
   const UINT32 numberWidth = 12;

   string out;
   out += "# " + CacheName + "\n";
   out += "# Cache Total Size : " + mydecstr(CacheSize, numberWidth) + "\n";
   out += "# Cache Line Size : "  +  mydecstr(CacheLineSize, numberWidth) + "\n";
   out += "# Cache Associativity : " +  mydecstr(CacheAssociativity, numberWidth/2) + "\n";

   return out;
} 

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
      + "  " +fltstr(1000.0 * Misses(tid) / GlobalInstCount, 2, 6) + "\n";
 
  out += "\n";

  return out;
}

string CACHE_BASE::StatsLongAll(string prefix, CACHE_TYPE cache_type) const
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
      + "  " +fltstr(1000.0 * MissesAll() / GlobalInstCount, 2, 6) + "\n";
 
  out += "\n";

  return out;
}

UINT64 CACHE_BASE::GetTheorectialEvictionCycles() const
{
  UINT64 Base=0;
  Base=CacheMaxSets*CacheAssociativity;
  Base=Base/0.36;
  double Scale=0.0;
  if (IsPrivate()) Scale=(double)MissesAll()/AccessesAll();
  else Scale=(double)Misses(0)/Accesses(0);
  return Base/Scale;
}

/// @ CACHE - brief cache class with specific cache set allocation policies
//  @ All that remains to be done here is allocate and deallocate the right
//  @ type of cache sets.
class CACHE : public CACHE_BASE
{
public:
  // constructors/destructors
  CACHE(std::string name, INT32 cacheLevel, UINT32 cacheSize, UINT32 lineSize, UINT32 associativity, std::string setType, UINT32 storeAlloc, OS_THREAD_ID threadID)
    : CACHE_BASE(name, cacheLevel, cacheSize, lineSize, associativity, setType, storeAlloc, threadID) {}

  /// Cache access from addr to addr+size-1
  bool Access(ADDRINT iaddr, ADDRINT addr, UINT32 size, ACCESS_TYPE accessType, THREADID tid);
  /// Cache access at addr that does not span cache lines
  bool AccessSingleLine(ADDRINT iaddr, ADDRINT addr, ACCESS_TYPE accessType, THREADID tid);

  /// Tell whether the line holding the given address exists.
  bool Probe(ADDRINT iaddr, ADDRINT addr, UINT32 size, ACCESS_TYPE accessType, THREADID tid);
  bool ProbeAll(ADDRINT iaddr, ADDRINT addr, UINT32 size, ACCESS_TYPE accessType, THREADID tid);

  /// Force the eviction.
  void ForceEvict(ADDRINT addr, UINT32 size,  THREADID tid);
  void ForceEvictAll(ADDRINT addr, UINT32 size );
};

#endif // PIN_CACHE_H
