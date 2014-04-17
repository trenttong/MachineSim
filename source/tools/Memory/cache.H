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
//
// @ORIGINAL_AUTHOR: Artur Klauser
//
// @AUTHOR: Xin Tong, University of Toronto

/*! @file
 *  This file contains a configurable cache class
 */

///--------------------------------------------------------------------------
//
// There are a few parameters that can be varied for instruction caches
//
// They are implemented to facilitate fast simulation setup.
//
// 1. cache size
//    - L1 instruction cache size in the state of the art processor 
//      ranges from 16 - 32 KB.
//
// 2. cache associativity 
//    - direct-mapped    : associativity == 1.
//    - set-associative  : associativity == n.
//    - fully-associative: associativity == cache size.
//
// 3. cache replacement policy
//    - least recently used   : replace the cache line that was accessed 
//                              least recenlty
//    - least frequently used : removes entities from the cache that are 
//                              used the least. 
//    - random replacement    : randomly select a candidate and replace.
//    - round robin           : replace the cache in a round robin fashion.
//    - etc
//
// In this tool, to initialize a cache, the following parameters need 
// to be given
// 
//  - std::string name  : name of the cache.
//  - UINT32 cacheSize  : size of the cache.
//  - UINT32 lineSize   : line size of the cache.
//  - UINT32 associativity : associativity of the set.
//  - UINT32 setType : type of the set.
//  - UINT32 storeAlloc : whether to allocate on store miss.
//
///--------------------------------------------------------------------------


#ifndef PIN_CACHE_H
#define PIN_CACHE_H

#define KILO 1024
#define MEGA (KILO*KILO)
#define GIGA (KILO*MEGA)

typedef UINT64 CACHE_STATS; // type of cache hit/miss counters

#include <iostream>
#include <fstream>
#include <cassert>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>

/*! RMR (rodric@gmail.com) 
 *   - temporary work around because decstr()
 *     casts 64 bit ints to 32 bit ones
 */
static string mydecstr(UINT64 v, UINT32 w)
{
    ostringstream o;
    o.width(w);
    o << v;
    string str(o.str());
    return str;
}

/*!
 *  @brief Checks if n is a power of 2.
 *  @returns true if n is power of 2
 */
static inline bool IsPower2(UINT32 n)
{
    return ((n & (n - 1)) == 0);
}

/*!
 *  @brief Computes floor(log2(n))
 *  Works by finding position of MSB set.
 *  @returns -1 if n == 0.
 */
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

/*!
 *  @brief Computes floor(log2(n))
 *  Works by finding position of MSB set.
 *  @returns -1 if n == 0.
 */
static inline INT32 CeilLog2(UINT32 n)
{
    return FloorLog2(n - 1) + 1;
}

/*!
 *  @brief Cache tag - self clearing on creation
 */
class CACHE_TAG
{
  private:
    ADDRINT _tag;

  public:
    CACHE_TAG(ADDRINT tag = 0) { _tag = tag; }
    bool operator==(const CACHE_TAG &right) const { return _tag == right._tag; }
    operator ADDRINT() const { return _tag; }
};


/*!
 * Everything related to cache sets
 *
 * To specify a cache set. Parameters which need to be specified are listed below.
 *
 * - associativity - how many lines are in the set
 * - replacement   - what is the replacement policy for the set.
 */
namespace CACHE_SET
{

typedef enum 
{
   CACHE_SET_BASE_SETTYPE,
   ROUND_ROBIN_SETTYPE,
   LRU_SETTYPE

} SET_TYPE;


/*!
 * This is the base of all cache sets. To add a new cache size, follow these steps.
 * 
 *  1. Add a subclass of CACHE_SET_BASE.
 *  2. Add a class type in the enum SET_TYPE.
 *  3. Add a check in class CACHE constructor.
 */ 
class CACHE_SET_BASE
{
  protected:
    // associativity of the cache size, this determines the number of 
    // cache lines a cache set holds.
    int _associativity;
    // all the tags in the cache size.
    CACHE_TAG *_tags;

  public:
    CACHE_SET_BASE(UINT32 associativity) 
       : _associativity(associativity),
         _tags(0)
    { 
    // allocate the tags here.
    _tags = new CACHE_TAG[_associativity]; 
    ASSERTX(_tags);
    for(INT32 index = _associativity - 1; index >= 0; index--)
       {
       _tags[index] = CACHE_TAG(0);
       }
    }

    virtual VOID   SetAssociativity(UINT32 associativity) { _associativity = associativity; }
    virtual UINT32 GetAssociativity(UINT32 associativity) { return _associativity; }

    // Find and Replace are the only two functions that need to be specialized.
    // Usually the Find function needs not be specialized as most cache implementation
    // shares the same idea.
    virtual UINT32 Find(CACHE_TAG  tag) 
    {  
        int cacheIdx = 0;
        for(cacheIdx = 0; cacheIdx < _associativity; cacheIdx++)
           {
           // found a matching line.
           if (_tags[cacheIdx] == tag) return true;
           }
        return false;
    }
    virtual VOID Replace(CACHE_TAG tag) { ASSERTX(0); }
};


/*!
 *  @brief Cache set with round robin replacement policy
 */
class ROUND_ROBIN_SET : public CACHE_SET_BASE
{
  private:
    UINT32 _tagsLastIndex;
    UINT32 _nextReplaceIndex;

  public:
    ROUND_ROBIN_SET(UINT32 associativity)
      : CACHE_SET_BASE(associativity), 
        _tagsLastIndex(associativity - 1), 
        _nextReplaceIndex(_tagsLastIndex)
    {
    // Intentionally left blank.
    }
    
    VOID Replace(CACHE_TAG tag)
    {
    // g++ -O3 too dumb to do CSE on following lines?!
    const UINT32 index = _nextReplaceIndex;

    _tags[index] = tag;
    // condition typically faster than modulo
    // replace the cache lines in a round robin fashion, starting from the
    // last cache line entry.
    _nextReplaceIndex = (index == 0 ? _tagsLastIndex : index - 1);
    }
};

/*!
 *  @brief Cache set with least recently used replacement policy
 */
class LRU_SET : public CACHE_SET_BASE
{
  private:
    // the use history of each cache line in the set. it is increment
    // on every access to this cache set.
    UINT32* _useHistory;

  public:
    LRU_SET(UINT32 associativity)
     : CACHE_SET_BASE(associativity) 
    {
    _useHistory = new UINT32[associativity];
    }

    virtual UINT32 Find(CACHE_TAG  tag) 
    {  
    int cacheIdx = 0;
    // increment the access history of the cache entry.
    for(cacheIdx = 0; cacheIdx < _associativity; cacheIdx++)
       _useHistory[cacheIdx] ++;

    for(cacheIdx = 0; cacheIdx < _associativity; cacheIdx++)
       {
       // found a matching line.
       if (_tags[cacheIdx] == tag) return true;
       }
    return false;
    }

    VOID Replace(CACHE_TAG tag)
    {
    // find the entry that is least recently used.
    INT32 currIndex = _associativity - 1;
    INT32 minIndex = currIndex; 
    for(;currIndex >= 0; currIndex--)
       {
       if (_useHistory[currIndex] >= _useHistory[minIndex]) 
          minIndex = currIndex;
       } 
    
    // minIndex contains the entry that was least recently accessed.
    // replace it.
    _tags[minIndex] = tag;
    _useHistory[minIndex] = 0;
    }
};
} // namespace CACHE_SET

namespace CACHE_ALLOC
{
    // do we allocate and evict if there is a store miss ?
    typedef enum 
    {
    STORE_ALLOCATE,
    STORE_NO_ALLOCATE
    } STORE_ALLOCATION;
}

/*!
 *  @brief Generic cache base class; no allocate specialization, no cache set specialization
 *
 *  This is the base class of all caches, some of the important cache parameters are listed
 *  below.
 *
 *  - _cacheSize - the total size of the cache
 *  - _lineSize  - the line size of the cache
 *  - _associativity - the associativity of the cache
 *  - _cacheConfig - the type of the cache. i.e. directly-mapped, etc.
 */
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

    typedef enum
    {
    CACHE_DIRECT_MAPPED,
    CACHE_SET_ASSOCIATIVE,
    CACHE_FULLY_ASSOCIATIVE
    } CACHE_CONFIG;

  protected:
    static const UINT32 HIT_MISS_NUM = 2;
    CACHE_STATS _access[ACCESS_TYPE_NUM][HIT_MISS_NUM];

    // the type of the set of the cache.
    UINT32 _setType;
    // the maximum number of sets in the cache.
    UINT32 _maxSets; 

    // all the sets in the cache.
    // this is a base class pointer. it can be used to point to
    // subclasses of the CACHE_SET::CACHE_SET_BASE.
    CACHE_SET::CACHE_SET_BASE **_sets;

    // whether to allocate on store misses ?
    UINT32 _storeAlloc;

  private:    // input params
    const std::string _name;
    const UINT32 _cacheSize;
    const UINT32 _lineSize;
    const UINT32 _associativity;
    UINT32 _cacheConfig;

    // computed params
    const UINT32 _lineShift;
    const UINT32 _setIndexMask;

    CACHE_STATS SumAccess(bool hit) const
    {
    CACHE_STATS sum = 0;

    for (UINT32 accessType = 0; accessType < ACCESS_TYPE_NUM; accessType++)
        {
        sum += _access[accessType][hit];
        }
    return sum;
    }

  protected:
    UINT32 NumSets() const { return _setIndexMask + 1; }

  public:
    // constructors/destructors
    CACHE_BASE(std::string name, UINT32 cacheSize, UINT32 lineSize, UINT32 associativity, std::string settype, UINT32 storealloc);

    // accessors
    UINT32 CacheSize() const { return _cacheSize; }
    UINT32 LineSize() const { return _lineSize; }
    UINT32 MaxSets() const { return _maxSets; }
    UINT32 StoreAlloc() const { return _storeAlloc; }
    UINT32 Associativity() const { return _associativity; }
    UINT32 CacheConfig() const { return _cacheConfig; }
    //
    CACHE_STATS Hits(ACCESS_TYPE accessType) const { return _access[accessType][true];}
    CACHE_STATS Misses(ACCESS_TYPE accessType) const { return _access[accessType][false];}
    CACHE_STATS Accesses(ACCESS_TYPE accessType) const { return Hits(accessType) + Misses(accessType);}
    CACHE_STATS Hits() const { return SumAccess(true);}
    CACHE_STATS Misses() const { return SumAccess(false);}
    CACHE_STATS Accesses() const { return Hits() + Misses();}

    VOID SplitAddress(const ADDRINT addr, CACHE_TAG & tag, UINT32 & setIndex) const
    {
    tag = addr >> _lineShift;
    setIndex = tag & _setIndexMask;
    }

    VOID SplitAddress(const ADDRINT addr, CACHE_TAG & tag, UINT32 & setIndex, UINT32 & lineIndex) const
    {
    const UINT32 lineMask = _lineSize - 1;
    lineIndex = addr & lineMask;
    SplitAddress(addr, tag, setIndex);
    }

    string StatsLong(string prefix = "", CACHE_TYPE = CACHE_TYPE_DCACHE) const;
};

CACHE_BASE::CACHE_BASE(std::string name, UINT32 cacheSize, UINT32 lineSize, UINT32 associativity, std::string setType, UINT32 storeAlloc)
  : _maxSets(cacheSize/(lineSize*associativity)),
    _storeAlloc(storeAlloc),
    _name(name),
    _cacheSize(cacheSize),
    _lineSize(lineSize),
    _associativity(associativity),
    _lineShift(FloorLog2(lineSize)),
    _setIndexMask((cacheSize / (associativity * lineSize)) - 1)
{

    ASSERTX(IsPower2(_lineSize));
    ASSERTX(IsPower2(_setIndexMask + 1));

    if (!strcmp("RR", setType.c_str()))
       {
       _setType = CACHE_SET::ROUND_ROBIN_SETTYPE;
       }
    else if (!strcmp("LRU", setType.c_str()))
       {
       _setType = CACHE_SET::LRU_SETTYPE;
       }
    else 
       {
       ASSERTX(0);
       }

    // set the the associativity type of the cache.
    if (associativity == 1)
       {
       // this is a directly mapped cache
       _cacheConfig = CACHE_DIRECT_MAPPED;
       }
    else if (associativity > 1 && associativity < cacheSize/lineSize)
       {
       // this is a set associatitve cache
       _cacheConfig = CACHE_SET_ASSOCIATIVE;
       }
    else if (associativity == cacheSize/lineSize)
       {
       // this is a fully associative cache
       _cacheConfig = CACHE_FULLY_ASSOCIATIVE;
       }
    else 
       {
       // how do we end up here ?
       ASSERTX(0);
       }
        
    for (UINT32 accessType = 0; accessType < ACCESS_TYPE_NUM; accessType++)
       {
       _access[accessType][false] = 0;
       _access[accessType][true] = 0;
       }
}

/*!
 *  @brief Stats output method
 */

string CACHE_BASE::StatsLong(string prefix, CACHE_TYPE cache_type) const
{
const UINT32 headerWidth = 19;
const UINT32 numberWidth = 12;
string out;
    
out += prefix + _name + ":" + "\n";

if (cache_type != CACHE_TYPE_ICACHE) 
   {
   // there are read and write accesses for data cache.
   for (UINT32 i = 0; i < ACCESS_TYPE_NUM; i++)
       {
       const ACCESS_TYPE accessType = ACCESS_TYPE(i);

       std::string type(accessType == ACCESS_TYPE_LOAD ? "Load" : "Store");

       out += prefix + ljstr(type + "-Hits:      ", headerWidth)
           + mydecstr(Hits(accessType), numberWidth)  +
           "  " +fltstr(100.0 * Hits(accessType) / Accesses(accessType), 2, 6) + "%\n";

       out += prefix + ljstr(type + "-Misses:    ", headerWidth)
           + mydecstr(Misses(accessType), numberWidth) +
           "  " +fltstr(100.0 * Misses(accessType) / Accesses(accessType), 2, 6) + "%\n";
        
       out += prefix + ljstr(type + "-Accesses:  ", headerWidth)
           + mydecstr(Accesses(accessType), numberWidth) +
           "  " +fltstr(100.0 * Accesses(accessType) / Accesses(accessType), 2, 6) + "%\n";
        
       out += prefix + "\n";
       }
    }

// there is only read access for instruction cache.
out += prefix + ljstr("Total-Hits:      ", headerWidth)
    + mydecstr(Hits(), numberWidth) +
    "  " +fltstr(100.0 * Hits() / Accesses(), 2, 6) + "%\n";

out += prefix + ljstr("Total-Misses:    ", headerWidth)
    + mydecstr(Misses(), numberWidth) +
    "  " +fltstr(100.0 * Misses() / Accesses(), 2, 6) + "%\n";

out += prefix + ljstr("Total-Accesses:  ", headerWidth)
    + mydecstr(Accesses(), numberWidth) +
    "  " +fltstr(100.0 * Accesses() / Accesses(), 2, 6) + "%\n";
out += "\n";

return out;
}


/*!
 *  @brief cache class with specific cache set allocation policies
 *
 *  All that remains to be done here is allocate and deallocate the right
 *  type of cache sets.
 */
class CACHE : public CACHE_BASE
{
  public:
    // constructors/destructors
    CACHE(std::string name, UINT32 cacheSize, UINT32 lineSize, UINT32 associativity, std::string setType, UINT32 storeAlloc)
      : CACHE_BASE(name, cacheSize, lineSize, associativity, setType, storeAlloc)
    {

    //_sets is an array of cache set pointers.
    _sets = new CACHE_SET::CACHE_SET_BASE*[NumSets()];
    for(UINT32 i = 0; i < NumSets(); i++)
       {
       // Initialize the sets. 
       if (_setType == CACHE_SET::ROUND_ROBIN_SETTYPE)
          {
          _sets[i] = new CACHE_SET::ROUND_ROBIN_SET(associativity);
          }
       else if (_setType == CACHE_SET::LRU_SETTYPE)
          {
          _sets[i] = new CACHE_SET::LRU_SET(associativity);
          }
       else 
          {
          ASSERTX(0);
          }
       ASSERTX(_sets[i]);
       _sets[i]->SetAssociativity(associativity);
       }
    }

    // modifiers
    /// Cache access from addr to addr+size-1
    bool Access(ADDRINT addr, UINT32 size, ACCESS_TYPE accessType);
    /// Cache access at addr that does not span cache lines
    bool AccessSingleLine(ADDRINT addr, ACCESS_TYPE accessType);
};

/*!
 *  @return true if all accessed cache lines hit
 */

bool CACHE::Access(ADDRINT addr, UINT32 size, ACCESS_TYPE accessType)
{
const ADDRINT highAddr = addr + size;
bool allHit = true;

const ADDRINT lineSize = LineSize();
const ADDRINT notLineMask = ~(lineSize - 1);
do
   {
   CACHE_TAG tag;
   UINT32 setIndex;

   SplitAddress(addr, tag, setIndex);

   CACHE_SET::CACHE_SET_BASE *set = _sets[setIndex];

   bool localHit = set->Find(tag);
   allHit &= localHit;

   // on miss, loads always allocate, stores optionally
   if ( (! localHit) && (accessType == ACCESS_TYPE_LOAD || _storeAlloc == CACHE_ALLOC::STORE_ALLOCATE))
      set->Replace(tag);

   addr = (addr & notLineMask) + lineSize; // start of next cache line
   }
   while (addr < highAddr);

_access[accessType][allHit]++;
return allHit;
}

/*!
 *  @return true if accessed cache line hits
 */
bool CACHE::AccessSingleLine(ADDRINT addr, ACCESS_TYPE accessType)
{
CACHE_TAG tag;
UINT32 setIndex;

SplitAddress(addr, tag, setIndex);

CACHE_SET::CACHE_SET_BASE* set = _sets[setIndex];

bool hit = set->Find(tag);

    // on miss, loads always allocate, stores optionally
if ( (! hit) && (accessType == ACCESS_TYPE_LOAD || _storeAlloc == CACHE_ALLOC::STORE_ALLOCATE))
    set->Replace(tag);

_access[accessType][hit]++;
return hit;
}

// define shortcuts

#endif // PIN_CACHE_H
