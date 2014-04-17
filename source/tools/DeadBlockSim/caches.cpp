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
#include "caches.H"

#include <map>
#include <set>
#include <vector>
#include <algorithm>


/* ===================================================================== */
/* Definition Flags */
/* ===================================================================== */
/// print whenever allocation happens.
#define ALLOC_DEBUG              0
/// print whenever a dead object is discovered.
#define DEAD_DEBUG               0
/// average life span tracking. print whenever an object is dead.
#define AVE_LS_DEBUG             0 
/// everytime a reference is destroyed. print what is left to reference the object.
#define REF_COUNT_SUB_DEBUG      0 
/// everytime a reference is added. print what is left to reference the object.
#define REF_COUNT_ADD_DEBUG      0

#define MAX_SIM_INST_COUNT       30000000000   // 10B instructions.
//#define MAX_SIM_INST_COUNT       500000000   // 10B instructions.

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<BOOL> KnobDisableData(KNOB_MODE_WRITEONCE, "pintool",
    "dd","0", "disable data cache simulation");
KNOB<BOOL> KnobDisableInstruction(KNOB_MODE_WRITEONCE, "pintool",
    "di","0", "disable instruction cache simulation");
KNOB<UINT32> KnobCoreCount(KNOB_MODE_WRITEONCE, "pintool",
    "core","4", "Number of cores in the simulated system");
KNOB<UINT32> KnobDisableLiveness(KNOB_MODE_WRITEONCE, "pintool",
    "dl","0", "Disable object liveness simulation");
KNOB<UINT32> KnobDisableCacheSim(KNOB_MODE_WRITEONCE, "pintool",
    "dc","0", "Disable Cache simulation");
KNOB<UINT32> KnobDisableTraceSim(KNOB_MODE_WRITEONCE, "pintool",
    "tc","1", "Disable Trace simulation");
KNOB<BOOL> KnobDisableLvl1Data(KNOB_MODE_WRITEONCE, "pintool",
    "dl1d","0", "disable level 1 data cache simulation");
KNOB<BOOL> KnobDisableLvl1Inst(KNOB_MODE_WRITEONCE, "pintool",
    "dl1i","0", "disable level 1 instruction cache simulation");
KNOB<BOOL> KnobDisableLvl2(KNOB_MODE_WRITEONCE, "pintool",
    "dl2","0", "disable level 2 cache simulation");
KNOB<BOOL> KnobDisableLvl3(KNOB_MODE_WRITEONCE, "pintool",
    "dl3","0", "disable level 3 cache simulation");
KNOB<BOOL> KnobEnableForceEvict(KNOB_MODE_WRITEONCE, "pintool",
    "fe","1", "enable force eviction");
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,    "pintool",
    "o", "caches.out", "specify icache file name");
KNOB<UINT32> KnobLineSize(KNOB_MODE_WRITEONCE, "pintool",
    "b","64", "cache block size in bytes");
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
    "a","4", "cache associativity (1 for direct mapped)");
KNOB<string> KnobSetType(KNOB_MODE_WRITEONCE, "pintool",
    "r","LRU", "cache replacement policy");
KNOB<UINT32> KnobWriteMissAllocate(KNOB_MODE_WRITEONCE, "pintool",
    "w","0", "write miss allocate (0 for allocate, 1 for not allocate ");
KNOB<UINT32> KnobDummyCacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "ldc","32", "L1 cache size in kilobytes");
KNOB<UINT32> KnobL1CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "l1c","32", "L1 cache size in kilobytes");
KNOB<UINT32> KnobL2CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "l2c","256", "L2 unified cache size in kilobytes");
KNOB<UINT32> KnobL3CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "l3c","2048", "L3 unified cache size in kilobytes PER CORE");
KNOB<UINT32> KnobSingleThread(KNOB_MODE_WRITEONCE, "pintool",
    "st","0", "Enable single thread profile");
KNOB<UINT32> KnobCacheDetailPrint(KNOB_MODE_WRITEONCE, "pintool",
    "dp","0", "Enable detailed private cache miss data");


/* ===================================================================== */
/* All the caches and auxiliary variables */
/* ===================================================================== */
CACHE* il1 =  NULL;
CACHE* dl1 =  NULL; 
CACHE* ul2 =  NULL;
CACHE* ul3 =  NULL;

CACHE* fil1 =  NULL;
CACHE* fdl1 =  NULL; 
CACHE* ful2 =  NULL;
CACHE* ful3 =  NULL;

FILE * Trace = NULL;

typedef struct { INT32 size; UINT64 klass; INT64 cycle; }ObjectMeta;
typedef std::map<ADDRINT, ObjectMeta> ObjectTypeMap;
typedef std::map<ADDRINT, std::set<ADDRINT>* > ObjectReferenceMap;
typedef std::vector<ADDRINT> ObjectReferenceArray;

typedef struct { ADDRINT addr; INT32 size; }ObjectInfo;
typedef std::map<ADDRINT, std::vector<ObjectInfo*>> ObjectRemovedMap;

static ObjectTypeMap OTM;
static ObjectReferenceMap ORM;
static ObjectReferenceArray ORA;
static ObjectRemovedMap OXM;

// Simulation currently enabled.
UINT64 Simulate=1;

PIN_MUTEX ObjectLTMutex;
PIN_MUTEX CacheSimMutex;

UINT64 GlobalInstCount                = 0;
UINT64 DeadAccessCount                = 0;
UINT64 GlobalL1DForceEvictCount       = 0;
UINT64 GlobalL2ForceEvictCount        = 0;
UINT64 GlobalL3ForceEvictCount        = 0;
UINT64 GlobalL1ICacheReplacementCount = 0;
UINT64 GlobalL1DCacheReplacementCount = 0;
UINT64 GlobalL2CacheReplacementCount  = 0;
UINT64 GlobalL3CacheReplacementCount  = 0;
UINT64 GlobalL1ICacheReplacementLength= 0;
UINT64 GlobalL1DCacheReplacementLength= 0;
UINT64 GlobalL2CacheReplacementLength = 0;
UINT64 GlobalL3CacheReplacementLength = 0;

UINT64 GlobalObjectAllocationCount    = 0;
UINT64 GlobalObjectAllocationSize     = 0;
UINT64 GlobalObjectDeadCount          = 0;
UINT64 GlobalObjectLifetimeLength     = 1000000;
UINT64 GlobalObjectDeadInLvl1Count    = 0;
UINT64 GlobalObjectDeadInLvl2Count    = 0;
UINT64 GlobalObjectDeadInLvl3Count    = 0;

typedef struct LiveOrDeath { UINT64 alloc; UINT64 dead; }  LiveOrDeath;
std::map<UINT64, LiveOrDeath>  AllocationByType;

/* ===================================================================== */
/* Instruction Printing for TraceBased Simulation */
/* ===================================================================== */
VOID DumpPC(VOID *ip, USIZE size, UINT32 memOperands)
{
	fprintf(Trace, "%p  %d  %d  ", ip, size, memOperands);
}

VOID RecordMemRead(VOID * ip, VOID * addr, USIZE size)
{
	fprintf(Trace,"0x00 %p %d\n", addr,size);
}

VOID RecordMemWrite(VOID * ip, VOID * addr, USIZE size)
{
	fprintf(Trace,"0x01 %p %d\n", addr, size);
}

VOID DumpInstructionBytes(VOID *ip, USIZE size)
{
	UINT8 i = 0;
	for (i=0; i<size; i++)
	{
		fprintf(Trace, "%x ",(*((UINT8*)ip+i)) );
	}
	fprintf(Trace,"\n");
}

// Pin calls this function every time a new instruction is encountered
VOID TraceSim(INS ins, VOID *v)
{
	USIZE size = INS_Size(ins);

    	UINT32 memOperands = INS_MemoryOperandCount(ins);

    	// Insert a call to DumpPC before every instruction, and pass it the IP
    	INS_InsertCall(ins, IPOINT_BEFORE, 
		      (AFUNPTR)DumpPC, 
                      IARG_INST_PTR, 
                      IARG_UINT32 , size , 
                      IARG_UINT32, memOperands, 
                      IARG_END);

    	INS_InsertCall(ins, IPOINT_BEFORE, 
                      (AFUNPTR)DumpInstructionBytes, 
                      IARG_INST_PTR, 
                      IARG_UINT32 , size , 
                      IARG_END);

    	// Iterate over each memory operand of the instruction.
    	for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    	{
    		if (INS_MemoryOperandIsRead(ins, memOp))
        	{
          		INS_InsertCall(ins, IPOINT_BEFORE, 
				      (AFUNPTR)RecordMemRead, 
                                      IARG_INST_PTR, 
                                      IARG_MEMORYOP_EA, memOp, 
                                      IARG_MEMORYREAD_SIZE, 
                                      IARG_END);
		}	

        	if (INS_MemoryOperandIsWritten(ins, memOp))
        	{
            		INS_InsertCall(ins, IPOINT_BEFORE, 
				      (AFUNPTR)RecordMemWrite,
                                      IARG_INST_PTR,
                                      IARG_MEMORYOP_EA, memOp, 
                                      IARG_MEMORYWRITE_SIZE,
                                      IARG_END);
       	 	}
    	}
}


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

     		CACHE_SET::CACHE_SET_BASE *set = GetCache(tid)->CacheSets[setindex];

     		bool localHit = set->Find(tag);

     		allHit &= localHit;
	
     		// on miss, loads always allocate, stores optionally
     		if (!localHit && (type == ACCESS_TYPE_LOAD || CacheStoreAlloc == STORE_ALLOCATE))
     		{
        		set->Replace(tag, iaddr);
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

  	CACHE_SET::CACHE_SET_BASE* set = GetCache(tid)->CacheSets[setindex];

  	bool hit = set->Find(tag);

  	// on miss, loads always allocate, stores optionally
  	if (!hit && (type == ACCESS_TYPE_LOAD || CacheStoreAlloc == STORE_ALLOCATE))
  	{
    		set->Replace(tag, iaddr);
  	}

  	GetCache(tid)->CacheAccess[type][hit]++;

  	return hit;
}


bool CACHE::Probe(ADDRINT iaddr, ADDRINT addr, UINT32 size, ACCESS_TYPE type, THREADID tid)
{
  	return Access(iaddr, addr, size, type, tid);
}
bool CACHE::ProbeAll(ADDRINT iaddr, ADDRINT addr, UINT32 size, ACCESS_TYPE type, THREADID tid)
{
	if (IsPrivate())
	{
		UINT32 index=0;
    		for(index = 0; index < MAX_CACHE_THREAD; index ++)
		{
			if (Probe(iaddr, addr, size, type, index)) return 1;
		}
		return 0;
	}
        else return Probe(iaddr, addr, size, type, 0);
}

void CACHE::ForceEvict(ADDRINT addr, UINT32 size, THREADID tid)
{
  	const ADDRINT highAddr = addr + size;
	const ADDRINT lineSize = GetLineSize();
  	const ADDRINT notLineMask = ~(lineSize - 1);
  	do 
  	{
     		CACHE_TAG tag;
     		UINT32 setindex;

     		SplitAddress(addr, tag, setindex);

     		CACHE_SET::CACHE_SET_BASE *set = GetCache(tid)->CacheSets[setindex];

     		bool localHit = set->Find(tag);
		
		if (localHit)
		{
			if(KnobEnableForceEvict.Value()) set->Free(tag, 0);
     		}
     		addr = (addr & notLineMask) + lineSize; // start of next cache line
  	} while (addr < highAddr);
}


void CACHE::ForceEvictAll(ADDRINT addr, UINT32 size)
{
	if (IsPrivate())
	{
		UINT32 index=0;
    		for(index = 0; index < MAX_CACHE_THREAD; index ++)
		{
			ForceEvict(addr, size, index);
		}
	}
        else return ForceEvict(addr, size, 0);
}

/* ===================================================================== */
/* Printing Routines */
/* ===================================================================== */
INT32 Usage()
{
  	cerr <<"This pin tool implements multiple levels of caches.\n\n";
  	cerr << KNOB_BASE::StringKnobSummary() << endl;
  	return -1;
}

LOCALFUN VOID Fini(int code, VOID * v)
{
  	int index = 0;
        char name[128];
        sprintf(name, "%s.%d", KnobOutputFile.Value().c_str(), getpid());
  	std::ofstream out(name);

  	out << "#========================================\n" << "# General stats\n" << "#========================================\n";
  	out << "# :" << endl;
  	out << "# " << GlobalInstCount << " instructions executed\n";
  	out << "# " << endl;

  	out << il1->StatsParam();
  	out << dl1->StatsParam();
  	out << ul2->StatsParam();
  	out << ul3->StatsParam();
  	out << "\n\n";

  	if (!KnobDisableLiveness.Value())
  	{
    		// Replacement Stats.
    		out << "#========================================\n" << "# Cache Replacement stats\n" << "#========================================\n";
    		out << "################\n" << "# L1 unified CACHE replacement length\n" << "################\n";
    		out << "# :" << endl;
		out << "# Theoretical Deadth-to-Eviction cycles: " << dl1->GetTheorectialEvictionCycles() << endl;
    		out << "# GlobalL1DCacheReplacementLength:" << GlobalL1DCacheReplacementLength/2 << " " 
                    << "GlobalL1DCacheReplacementCount:" << GlobalL1DCacheReplacementCount/2 << endl;
    		out << "# " << GlobalL1DCacheReplacementLength/GlobalL1DCacheReplacementCount << " Instructions Per Replacement\n";
		out << "# GlobalL1DForceEvictCount:" << GlobalL1DForceEvictCount << "\n\n";

    		out << "################\n" << "# L2 unified CACHE replacement length\n" << "################\n";
    		out << "# :" << endl;
		out << "# Theoretical Deadth-to-Eviction cycles: " << ul2->GetTheorectialEvictionCycles() << endl;
    		out << "# GlobalL2CacheReplacementLength:" << GlobalL2CacheReplacementLength/2 << " " 
                    << "GlobalL2CacheReplacementCount:" << GlobalL2CacheReplacementCount/2 << endl;
    		out << "# " << GlobalL2CacheReplacementLength/GlobalL2CacheReplacementCount << " Instructions Per Replacement\n";
		out << "# GlobalL2ForceEvictCount:" << GlobalL2ForceEvictCount << "\n\n";

    		out << "################\n" << "# L3 unified CACHE replacement length\n" << "################\n";
    		out << "# :" << endl;
		out << "# Theoretical Deadth-to-Eviction cycles: " << ul3->GetTheorectialEvictionCycles() << endl;
    		out << "# GlobalL3CacheReplacementLength:" << GlobalL3CacheReplacementLength/2 << " " 
                    << "GlobalL3CacheReplacementCount:" << GlobalL3CacheReplacementCount/2 << endl;
    		out << "# " << GlobalL3CacheReplacementLength/GlobalL3CacheReplacementCount << " Instructions Per Replacement\n";
		out << "# GlobalL3ForceEvictCount:" << GlobalL3ForceEvictCount << "\n\n";

    		// Object Stats.
    		out << "#========================================\n" << "# VM stats\n" << "#========================================\n";
    		out << "################\n" << "# Virtual Machine Object Lifetime length\n" << "################\n";
    		out << "# :" << endl;
    		out << "# GlobalObjectAllocationCount:" << GlobalObjectAllocationCount << " GlobalObjectDeadCount:" << GlobalObjectDeadCount 
		    << " survival rate:" << (double)GlobalObjectDeadCount/GlobalObjectAllocationCount << " AverageSize:" << GlobalObjectAllocationSize/GlobalObjectAllocationCount << "\n";
    		out << "# GlobalObjectDeadCount:" << GlobalObjectDeadCount << " " << "GlobalObjectLifetimeLength:" << GlobalObjectLifetimeLength << endl;
		out << "# GlobalObjectDeadInLvl1Count:" << GlobalObjectDeadInLvl1Count << " " << (double)GlobalObjectDeadInLvl1Count/GlobalObjectDeadCount*100 << "%" << endl;
		out << "# GlobalObjectDeadInLvl2Count:" << GlobalObjectDeadInLvl2Count << " " << (double)GlobalObjectDeadInLvl2Count/GlobalObjectDeadCount*100 << "%" << endl;
		out << "# GlobalObjectDeadInLvl3Count:" << GlobalObjectDeadInLvl3Count << " " << (double)GlobalObjectDeadInLvl3Count/GlobalObjectDeadCount*100 << "%" << endl;
		out << "# DeadAccessCount:" << DeadAccessCount << endl;
    		out << "# " << GlobalObjectLifetimeLength/GlobalObjectDeadCount << " Instructions Per Object\n#\n";

		out << "# Object Allocation Distribution\n";
#if 0
		for(std::map<UINT64, LiveOrDeath>::iterator I=AllocationByType.begin(); I!=AllocationByType.end(); I++)
		{
			out << "# 0x" << hex << I->first << " : "  << dec << I->second.alloc << " : " <<  I->second.dead << endl;
		}
#endif
		out << "Number of Pages Discovered " << OXM.size() << endl;
		out << "\n";
  	}

  	out << "#========================================\n" << "# Cache Hit/Miss stats\n" << "#========================================\n";

  	if (KnobCacheDetailPrint.Value() && !KnobDisableLvl1Inst.Value())
  	{
     		for(index = 0; index < MAX_CACHE_THREAD; index ++)
     		{
			if (il1->GetUsed(index))
        		{
     	  			out << "################\n" << "# L1 ICACHE stats\n" << "################\n";
     	  			out << il1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_ICACHE, index);
        		}
     		}
  	}
  	if (KnobCacheDetailPrint.Value() && !KnobDisableLvl1Data.Value())
  	{
     		for(index = 0; index < MAX_CACHE_THREAD; index ++)
     		{
			if (dl1->GetUsed(index))
        		{
     	  			out << "################\n" << "# L1 DCACHE stats\n" << "################\n";
     	  			out << dl1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE, index);
			}
     		}
  	}
  	if (KnobCacheDetailPrint.Value() && !KnobDisableLvl2.Value())
  	{
     		for(index = 0; index < MAX_CACHE_THREAD; index ++)
     		{
			if (ul2->GetUsed(index))
        		{
          			out << "################\n" << "# L2 unified CACHE stats\n" << "################\n";
          			out << ul2->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE, index);
			}
     		}
  	}

        // Print the SUM of simulation results.
  	if (!KnobDisableLvl1Inst.Value())
  	{
    		out << "################\n" << "# L1 ICACHE stats\n" << "################\n";
    		out << il1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
       		il1->Shutdown();
    		out << "################\n" << "# L1 ForceEvict ICACHE stats\n" << "################\n";
    		out << fil1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
       		fil1->Shutdown();
  	}
  	if (!KnobDisableLvl1Data.Value())
  	{
    		out << "################\n" << "# L1 DCACHE stats\n" << "################\n";
    		out << dl1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
       		dl1->Shutdown();

    		out << "################\n" << "# L1 ForceEvict DCACHE stats\n" << "################\n";
    		out << fdl1->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
       		fdl1->Shutdown();
  	}
  	if (!KnobDisableLvl2.Value())
  	{
     		out << "################\n" << "# L2 unified CACHE stats\n" << "################\n";
     		out << ul2->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
       		ul2->Shutdown();

     		out << "################\n" << "# L2 ForceEvict unified CACHE stats\n" << "################\n";
     		out << ful2->StatsLongAll("# ", CACHE_BASE::CACHE_TYPE_DCACHE);
       		ful2->Shutdown();
  	}
  	if (!KnobDisableLvl3.Value())
  	{
     		out << "################\n" << "# L3 unified CACHE stats\n" << "################\n";
     		out << ul3->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE, 0);
     		ul3->Shutdown();

     		out << "################\n" << "# L3 ForceEvict unified CACHE stats\n" << "################\n";
     		out << ful3->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE, 0);
     		ful3->Shutdown();
  	}

  	return;
}


/* ===================================================================== */
/* Object Liveness Tracking... */
/* ===================================================================== */
/// @ --------------------------------------------------------------------------
//  @ Object liveness tracking is a diffcult task. 
//  @
//  @ what infORMation do we have ?
//  @  - the address of the object when it is allocated.
//  @  - the size of the object that is allocated.
//  @ 
//  @ we need to keep 1 structure mapping object address to memory address holding 
//  @ references to it. and when the fields are updated, we update map as well. 
//  @ when the map goes to 0, we know there is no one holding this object anymore.
//  @
//  @ we will also need a map holding memory address and the object it holds.
//  @ if this memory address is modified, we will need to update the object.
//  @
//  @ this involves watching every memory access. i.e. on every memory access
//  @ hash into this mapping and see whether it clobbers a field that is holding
//  @ a reference to an object. 
//  @
//  @ in case an object is discovered to be dead, we will need to take its memory 
//  @ region and do a reverse lookup and find all the memory within it that potentially
//  @ holds a reference to a live object. this can trigger a ripple effect, i.e. a chain
//  @ of dead objects being discovered. 
//  @ 
//  @ this algorithm could turn out to be a very time consuming process and therefore
//  @ it could make the simulator very slow.
//  @ 
//  @ a separate circuit can be made to do this in hardware.
/// @ --------------------------------------------------------------------------
#define HEAP_MAX_ADDRESS 0x7FFFFFFFF 
#define HEAP_MIN_ADDRESS 0x700000000

/* =============================================== */
/* Utility Functions ... */
/* =============================================== */
static inline bool ValidRange(ADDRINT Addr)
{
	return Addr >= HEAP_MIN_ADDRESS && Addr <= HEAP_MAX_ADDRESS;
}
static inline bool ValidObject(ADDRINT Addr)
{
   	return OTM.find(Addr) != OTM.end();
}
static inline bool InvalidObject(ADDRINT Addr)
{
   	return OTM.find(Addr) == OTM.end();
}
static inline bool UnknownObject(ADDRINT Addr)
{
   	return OTM.find(Addr) == OTM.end();
}

/* =============================================== */
/* Object Reference Tracking  Functions ... */
/* =============================================== */
static void removeObjectRef(ADDRINT Obj, ADDRINT RefAddr)
{
   	// Assume global lock is held prior to entering this function.
   	std::set<ADDRINT> *Refs=0;
   	Refs=ORM[Obj];
	if (!Refs) return;
   	std::set<ADDRINT>::iterator I = find(Refs->begin(), Refs->end(), RefAddr);
   	if (I!=Refs->end()) 
   	{
     		Refs->erase(I);

#if REF_COUNT_SUB_DEBUG 
		cout << "after subing reference 0x" << hex << Obj << " reference count is " << Refs->size() << endl;
		for(I=Refs->begin();I!=Refs->end(); I++)
		{
			cout << "Reference address 0x" << hex <<  *I << endl;
		}
#endif
   	}

   	if (Refs->size() == 0)
   	{
      		// This object is dead. But discount the outliers.
		if (GlobalInstCount - OTM[Obj].cycle < 8*GlobalObjectLifetimeLength/(GlobalObjectDeadCount+1))
		{
      			GlobalObjectDeadCount ++;
      			GlobalObjectLifetimeLength += GlobalInstCount - OTM[Obj].cycle;

			AllocationByType[OTM[Obj].klass].dead ++;

			// How many lines still in level 1 cache.
        		PIN_MutexLock(&CacheSimMutex);
			//fdl1->ForceEvictAll(Obj, 1);
			//ful2->ForceEvictAll(Obj, 1);
			ful3->ForceEvict(Obj, 1, 0);
        		PIN_MutexUnlock(&CacheSimMutex);
		}
#if DEAD_DEBUG
		cout << GlobalObjectDeadCount << ": Object 0x" << hex << Obj << " discovered to be dead" << endl;
#endif 
	
#if AVE_LS_DEBUG
		cout << "Current Average GlobalObjectLifeSpan " << GlobalObjectLifetimeLength/GlobalObjectDeadCount << endl;
#endif
		// The objects pointed to this object could be dead as well.
		UINT32 offset=0;
		UINT32 size=OTM[Obj].size;
		for(offset=0; offset<size; offset++)
		{
			ADDRINT PAddr = Obj+offset;
			removeObjectRef(*(UINT64*)PAddr, PAddr);
		}
		
		ObjectInfo *OM = new ObjectInfo;
		OM->addr = Obj;
  		OM->size = size;

		OXM[Obj & ~0xFFF].push_back(OM);

      		// Remove this object from the maps.
      		OTM.erase(OTM.find(Obj));
      		ORM.erase(ORM.find(Obj));
      		delete (Refs);
   	}
}
static void createObjectRef(ADDRINT Obj, ADDRINT RefAddr)
{
   	// Assume global lock is held prior to entering this function.
   	std::set<ADDRINT> *Refs=0;
   	Refs=ORM[Obj];
   	ASSERTX(Refs);
   	Refs->insert(RefAddr);
#if REF_COUNT_ADD_DEBUG
	cout << "after adding reference 0x" << hex << Obj << " reference count is " << Refs->size() << endl;
	for(I=Refs->begin();I!=Refs->end(); I++)
	{
		cout << "Reference address 0x" << hex <<  *I << endl;
	}
#endif
}


/* =============================================== */
/* PIN instrumentation Reference Count Functions.  */
/* =============================================== */
LOCALFUN VOID instCountFun(ADDRINT Addr)
{
	if (!Simulate) return;

  	// one more instructions executed.
  	// Multiple threads could make this number bigger ...
  	// In priniciple, there must be a factor to dampen this.
  	// as this is not a measurement of wall clock time.
  	// However,we do not need to care as we are dealing with
  	// LLC cache here which is shared. GlobalInstCount is more
  	// like a measurement on the number of instructions that have 
  	// been seen between the fill and eviction of a cache block
  	// and the death of the objects. we are comparing apple to apple
  	// here.
  	PIN_MutexLock(&ObjectLTMutex);
  	GlobalInstCount ++;
  	PIN_MutexUnlock(&ObjectLTMutex);

	// Detach when a fixed number of instructions are simulated.
	if (GlobalInstCount > MAX_SIM_INST_COUNT) 
	{
		cout << "Simulation reach max instruction number " << GlobalInstCount << endl;
		Simulate=0;
	}
}

// allocFun is called when a special allocation sequence is encountered.
LOCALFUN VOID allocFun(ADDRINT Addr)
{
	if (!Simulate) return;

	/// std C++ library structures may not be thread safe...
  	PIN_MutexLock(&ObjectLTMutex);

  	UINT8 ret = *(UINT8*)(Addr+1);

  	INT64 Size = *(UINT64*) (Addr+2+sizeof(UINT64));

  	INT64 Type = *(UINT64*) (Addr+2+2*sizeof(UINT64));

  	// the address of the object is written into the quadword after the addr+2.
  	// this is the channel between the JVM and PIN.
  	Addr = *(UINT64*) (Addr+2);

  	// A new object created ?.
  	if (ret==0xC3 && ValidRange(Addr) && UnknownObject(Addr))
  	{
    		// Create the structure to hold this object and its metadata.
    		OTM[Addr] = {Size, Type, GlobalInstCount};
    		ORM[Addr] = new std::set<ADDRINT>;
	
    		GlobalObjectAllocationSize += Size;
    		GlobalObjectAllocationCount ++;

		AllocationByType[Type].alloc ++;

#if ALLOC_DEBUG
		cout << GlobalObjectAllocationCount << ": New object allocated 0x" << hex << Addr << endl;
#endif
  	}

  	PIN_MutexUnlock(&ObjectLTMutex);
}

LOCALFUN VOID clobberRFun(REG RegNum, ADDRINT OldRegValue, THREADID tid)
{
	if (!Simulate) return;

	/// std C++ library structures may not be thread safe...
	PIN_MutexLock(&ObjectLTMutex);

	ASSERTX(RegNum<16);

	// A new reference to the object created.	
	if (ValidObject(OldRegValue))
	{
		// This register was holding a reference to another object.
   		removeObjectRef(OldRegValue, RegNum<<8 | tid); 
	}
   	PIN_MutexUnlock(&ObjectLTMutex);
}

LOCALFUN VOID clobberMFun(ADDRINT Mem)
{
	if (!Simulate) return;

	/// std C++ library structures may not be thread safe...
	PIN_MutexLock(&ObjectLTMutex);
	
	UINT64 OldRegValue = *(UINT64*) Mem;

	// A new reference to the object created.	
	if (ValidObject(OldRegValue))
	{
		// This register was holding a reference to another object.
   		removeObjectRef(OldRegValue, Mem); 
	}
   	PIN_MutexUnlock(&ObjectLTMutex);
}

LOCALFUN VOID createRMFun(REG RegNum, ADDRINT Mem, THREADID tid)
{
	if (!Simulate) return;

	/// std C++ library structures may not be thread safe...
	PIN_MutexLock(&ObjectLTMutex);

	ASSERTX(RegNum<16);
	
	UINT64 OldRegValue = *(UINT64*) Mem;

	// A new reference to the object created.	
	if (ValidObject(OldRegValue))
	{
		// This register was holding a reference to another object.
   		createObjectRef(OldRegValue, RegNum<<8 | tid); 
	}
   	PIN_MutexUnlock(&ObjectLTMutex);
}

LOCALFUN VOID createRRFun(REG RegNum, ADDRINT RegVal, THREADID tid)
{
	if (!Simulate) return;

	/// std C++ library structures may not be thread safe...
	PIN_MutexLock(&ObjectLTMutex);
	
	UINT64 OldRegValue = RegVal;

	// A new reference to the object created.	
	if (ValidObject(OldRegValue))
	{
		// This register was holding a reference to another object.
   		createObjectRef(OldRegValue, RegNum<<8 | tid); 
	}
   	PIN_MutexUnlock(&ObjectLTMutex);
}

LOCALFUN VOID createMRFun(ADDRINT Mem, UINT64 OVal)
{
	if (!Simulate) return;

	/// std C++ library structures may not be thread safe...
	PIN_MutexLock(&ObjectLTMutex);

	UINT64 OldRegValue = OVal;

	// A new reference to the object created.	
	if (ValidObject(OldRegValue))
	{
		// This register was holding a reference to another object.
   		createObjectRef(OldRegValue, Mem); 
	}
   	PIN_MutexUnlock(&ObjectLTMutex);
}

std::map<INT32, ADDRINT> ModAddr;

LOCALFUN VOID recordMFun(ADDRINT Mem, THREADID tid)
{
   ModAddr[tid] = Mem;
}

LOCALFUN VOID createMFun(THREADID tid)
{
	if (!Simulate) return;

	/// std C++ library structures may not be thread safe...
	PIN_MutexLock(&ObjectLTMutex);

	ADDRINT Mem=ModAddr[tid];

	UINT64 OldRegValue = *(UINT64*) Mem;

	// A new reference to the object created.	
	if (ValidObject(OldRegValue))
	{
		// This register was holding a reference to another object.
   		createObjectRef(OldRegValue, Mem); 
	}
   	PIN_MutexUnlock(&ObjectLTMutex);
}


/* ===================================================================== */
/* Cache Access Routines */
/* ===================================================================== */

LOCALFUN VOID Ul2Access(ADDRINT iaddr, ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE type, THREADID tid)
{
	if (!Simulate) return;

	if (1)
	{
  	  	// second level unified cache
  	  	BOOL ul2Hit = 0;
  	  	if (ul2) ul2Hit = ul2->Access(iaddr, addr, size, type, tid);

  	  	// third level unified cache

  	  	// level 3 cache is shared ... it could be access concurrently by different threads.
  	  	PIN_MutexLock(&CacheSimMutex);
  	  	if (!ul2Hit && ul3) ul3->Access(iaddr, addr, size, type, tid);
  	  	PIN_MutexUnlock(&CacheSimMutex);
	}

	if (1)
	{
  	  	// second level unified cache
  	  	BOOL ful2Hit = 0;
  	  	if (ful2) ful2Hit = ful2->Access(iaddr, addr, size, type, tid);

  	  	// third level unified cache

  	  	// level 3 cache is shared ... it could be access concurrently by different threads.
  	  	PIN_MutexLock(&CacheSimMutex);
  	  	if (!ful2Hit && ful3) ful3->Access(iaddr, addr, size, type, tid);
  	  	PIN_MutexUnlock(&CacheSimMutex);
	}
}

LOCALFUN VOID InstCount(ADDRINT addr, THREADID tid)
{
	if (!Simulate) return;

	GlobalInstCount ++;
}

LOCALFUN VOID InsRef(ADDRINT addr, THREADID tid)
{
	if (!Simulate) return;

  	const CACHE_BASE::ACCESS_TYPE type = CACHE_BASE::ACCESS_TYPE_LOAD;
	
	if (1)
	{
  		// first level I-cache
  		BOOL il1Hit = 0;
  		if (il1) il1Hit = il1->AccessSingleLine(addr, addr, type, tid);

  		// second level unified Cache
  		if (!il1Hit) Ul2Access(addr, addr, 1, type, tid);
	}

	if (1)
	{
  		// first level I-cache
  		BOOL fil1Hit = 0;
  		if (fil1) fil1Hit = fil1->AccessSingleLine(addr, addr, type, tid);

  		// second level unified Cache
  		if (!fil1Hit) Ul2Access(addr, addr, 1, type, tid);
	}
	
	// 1 more instruction executed.
	instCountFun(addr);
}

LOCALFUN VOID MemRefMulti(ADDRINT iaddr, ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE type, THREADID tid)
{
	if (!Simulate) return;

#if  0 
	std::vector<ObjectInfo*>::iterator I=OXM[addr & ~63].begin();
	std::vector<ObjectInfo*>::iterator K=OXM[addr & ~63].end();
	for (;I!=K;I++)
	{
		if (addr >= (*I)->addr && addr+size <= (*I)->addr+(*I)->size)
		{
			DeadAccessCount ++;
			return;
		}
	}
#endif
	if (1)
	{
	  	// first level D-cache
 	  	BOOL dl1Hit = 0;
	  	if (dl1) dl1Hit = dl1->Access(iaddr, addr, size, type, tid);

	  	// second level unified Cache
	  	if (!dl1Hit) Ul2Access(iaddr, addr, size, type, tid);
	}

	if (1)
	{
	  	// first level D-cache
 	  	BOOL fdl1Hit = 0;
	  	if (fdl1) fdl1Hit = fdl1->Access(iaddr, addr, size, type, tid);

	  	// second level unified Cache
	  	if (!fdl1Hit) Ul2Access(iaddr, addr, size, type, tid);
	}
}

LOCALFUN VOID MemRefSingle(ADDRINT iaddr, ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE type, THREADID tid)
{
	if (!Simulate) return;

#if  0 
	std::vector<ObjectInfo*>::iterator I=OXM[addr & ~63].begin();
	std::vector<ObjectInfo*>::iterator K=OXM[addr & ~63].end();
	for (;I!=K;I++)
	{
		if (addr >= (*I)->addr && addr+size <= (*I)->addr+(*I)->size)
		{
			DeadAccessCount ++;
			return;
		}
	}

#endif

	if (1)
	{
	  // first level D-cache
	  BOOL dl1Hit = 0;
  	  if (dl1) dl1Hit = dl1->AccessSingleLine(iaddr, addr, type, tid);

  	  // second level unified Cache
  	  if (!dl1Hit) Ul2Access(iaddr, addr, size, type, tid);
	}

	if (1)
	{
	  // first level D-cache
	  BOOL fdl1Hit = 0;
  	  if (fdl1) fdl1Hit = fdl1->AccessSingleLine(iaddr, addr, type, tid);

  	  // second level unified Cache
  	  if (!fdl1Hit) Ul2Access(iaddr, addr, size, type, tid);
	}
}

LOCALFUN VOID Liveness(INS ins, VOID *v)
{
  /// =========================================================
  //  Object Liveness Tracking Callbacks.
  /// =========================================================

  // Local variables.
  UINT32 i=0;

  if (INS_IsNop(ins))
  {
     /// --------------------------------------------- ///
     // A possible new object allocation discovered.
     /// --------------------------------------------- ///
     INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                              (AFUNPTR)allocFun,
                              IARG_INST_PTR,
                              IARG_END);

  }

  /// =========================================================
  // the object reference the instruction could create.
  // instructions considered so far.
  /// =========================================================
  if (INS_HasFallThrough(ins))
  {
     /// --------------------------------------------- ///
     // any instructions that can create a reference. 
     /// --------------------------------------------- ///
     UINT32 operandCount = INS_OperandCount (ins);
     REG dst=REG_INVALID();
     for (i=0; i<operandCount; i++)
     {
        if (INS_OperandWritten (ins, i) && INS_OperandIsReg(ins, i))
        {
          dst = INS_OperandReg(ins, i); 
        }
     }

     if (dst!=REG_INVALID())
     {
	if (REG_is_gr(dst))
	{
           INS_InsertPredicatedCall(ins, IPOINT_AFTER, 
                                   (AFUNPTR)createRRFun,
                                   IARG_UINT32, dst-REG_RBASE,   // RegNum 
                                   IARG_REG_VALUE, dst,          // OldRegVal 
				   IARG_THREAD_ID,
                                   IARG_END);
	} 

	if (0 && REG_is_fr(dst)) 
	{
           INS_InsertPredicatedCall(ins, IPOINT_AFTER, 
                                   (AFUNPTR)createRRFun,
                                   IARG_UINT32, dst-REG_RBASE,   // RegNum 
                                   IARG_REG_REFERENCE, dst,          // OldRegVal 
				   IARG_THREAD_ID,
                                   IARG_END);
	}
     }
  }

  if (INS_IsMemoryWrite(ins) && INS_HasFallThrough(ins))
  {
     /// --------------------------------------------- ///
     // any instructions that can create a reference. 
     /// --------------------------------------------- ///
     INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                             (AFUNPTR)recordMFun,
              		     IARG_MEMORYWRITE_EA,     // Mem to be Clobbered.
			     IARG_THREAD_ID,
                             IARG_END);

     INS_InsertPredicatedCall(ins, IPOINT_AFTER, 
                             (AFUNPTR)createMFun,
		             IARG_THREAD_ID,
                             IARG_END);

  }


  /// =========================================================
  ///  the object reference the instruction could clobber.
  /// =========================================================
  if (1)
  {
     /// --------------------------------------------- ///
     // any instructions that could clobber a reg. 
     /// --------------------------------------------- ///
     UINT32 operandCount = INS_OperandCount (ins);
     REG dst=REG_INVALID();
     for (i=0; i<operandCount; i++)
     {
        if (INS_OperandWritten (ins, i) && INS_OperandIsReg(ins, i))
        {
          dst = INS_OperandReg(ins, i); 
        }
     }

     if (dst!=REG_INVALID() && REG_is_gr(dst))
     {
           INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                   (AFUNPTR)clobberRFun,
                                   IARG_UINT32, dst-REG_RBASE,   // RegNum 
                                   IARG_REG_VALUE, dst,          // OldRegVal 
				   IARG_THREAD_ID,
                                   IARG_END);
     }
  }

  if (1)
  {
     /// --------------------------------------------- ///
     // any instructions that could clobber a memory. 
     /// --------------------------------------------- ///
     UINT32 operandCount = INS_OperandCount (ins);
     UINT32 memClobber=0;
     for (i=0; i<operandCount; i++)
     {
        if (INS_OperandWritten(ins, i) && INS_OperandIsMemory(ins, i))
        {
	   memClobber=1;
        }
     }

     if (memClobber)
     {
           INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                   (AFUNPTR)clobberMFun,
				   IARG_MEMORYWRITE_EA,     // Mem to be Clobbered.
                                   IARG_END);
     }
  }
}

LOCALFUN VOID CacheSim(INS ins,VOID *v)
{
  	/// =========================================================
  	//  Cache Simulation Callbacks.
  	/// =========================================================

  	/// --------------------------------------------- ///
  	//  instruction count                              //
  	/// --------------------------------------------- ///
  	if (KnobDisableInstruction.Value())
  	{
    		// all instruction fetches access I-cache
    		// assume instruction access does not cross cache line.
    		INS_InsertCall(ins, IPOINT_BEFORE, 
                   		(AFUNPTR)InstCount,
                   		IARG_INST_PTR,
                   		IARG_THREAD_ID,
                   		IARG_END);
  	}
	

  	/// --------------------------------------------- ///
  	//  instruction cache simulation                   //
  	/// --------------------------------------------- ///
  	if (!KnobDisableInstruction.Value())
  	{
    		// all instruction fetches access I-cache
    		// assume instruction access does not cross cache line.
    		INS_InsertCall(ins, IPOINT_BEFORE, 
                   		(AFUNPTR)InsRef,
                   		IARG_INST_PTR,
                   		IARG_THREAD_ID,
                   		IARG_END);
  	}

  	/// --------------------------------------------- ///
  	//  data cache simulation                          //
  	/// --------------------------------------------- ///
  	if (!KnobDisableData.Value())
  	{
    		if (INS_IsMemoryRead(ins))
    		{
       			const UINT32 size = INS_MemoryReadSize(ins);
       			const AFUNPTR countFun = (size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

       			/// --------------------------------------------- ///
       			//  Read an data memory location                   //
       			/// --------------------------------------------- ///
       			// only predicated-on memory instructions access D-cache
       			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                		(AFUNPTR)countFun,
                                		IARG_INST_PTR,
                                		IARG_MEMORYREAD_EA,
                                		IARG_MEMORYREAD_SIZE,
                                		IARG_UINT32, CACHE_BASE::ACCESS_TYPE_LOAD,
                                		IARG_THREAD_ID,
                                		IARG_END);
    		}

    		if (INS_IsMemoryWrite(ins))
    		{
       			const UINT32 size = INS_MemoryWriteSize(ins);
       			const AFUNPTR countFun = (size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

       			/// --------------------------------------------- ///
       			//  Write an data memory location                  //
       			/// --------------------------------------------- ///
       			// only predicated-on memory instructions access D-cache
       			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                		(AFUNPTR)countFun,
                                		IARG_INST_PTR,
                                		IARG_MEMORYWRITE_EA,
                                		IARG_MEMORYWRITE_SIZE,
                                		IARG_UINT32, CACHE_BASE::ACCESS_TYPE_STORE,
                                		IARG_THREAD_ID,
                                		IARG_END);
    		}
  	}
}

LOCALFUN VOID Instruction(INS ins, VOID *v)
{
        // Object liveness tracking enabled.
  	if (!KnobDisableLiveness.Value())
  	{
    		Liveness(ins, v);
  	}

        // Cache simulation enabled.
  	if (!KnobDisableCacheSim.Value())
  	{
    		CacheSim(ins, v);
  	}

        // Trace base simulation enabled.
  	if (!KnobDisableTraceSim.Value())
  	{
    		TraceSim(ins, v);
  	}
}


/* ===================================================================== */
/* The fun begins ... */
/* ===================================================================== */
GLOBALFUN int main(int argc, char *argv[])
{
  	if (PIN_Init(argc,argv)) return Usage();

  	// Initialize the mutex to guard for MT.
  	PIN_MutexInit(&ObjectLTMutex);
  	PIN_MutexInit(&CacheSimMutex);

	// Open tracefile if trace simulation is enabled.
        if (!KnobDisableTraceSim.Value())
	{
		char name[128];
                sprintf(name, "%s.%d", "SimTrace.out", getpid());
		Trace=fopen(name,"w");
	}

  	if (!KnobDisableLvl1Inst.Value())
  	{
     		// level 1 instruction cache.
     		il1 = new CACHE("L1 Inst Cache", 1,
                     		KnobL1CacheSize.Value() * KILO,
                     		KnobLineSize.Value(),
                     		KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());

     		fil1 = new CACHE("L1 Inst Cache", 1,
                     		KnobL1CacheSize.Value() * KILO,
                     		KnobLineSize.Value(),
                     		KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());

  	}
  	if (!KnobDisableLvl1Data.Value())
  	{
     		// level 1 data cache.
     		dl1 = new CACHE("L1 Data Cache", 1,
                     		KnobL1CacheSize.Value() * KILO,
                     		KnobLineSize.Value(),
                     		KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());
     		fdl1 = new CACHE("L1 Data Cache", 1,
                     		KnobL1CacheSize.Value() * KILO,
                     		KnobLineSize.Value(),
                     		KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());
 
  	}
  	if (!KnobDisableLvl2.Value())
  	{
     		// level 1 must be simulated if level 2 is simulated.
     		ASSERTX(!KnobDisableLvl1Inst.Value() && !KnobDisableLvl1Data.Value());
     		// level 2 unified cache
     		ul2 = new CACHE("L2 Unified Cache", 2,
                     		KnobL2CacheSize.Value() * KILO,
                     		KnobLineSize.Value(),
                     		KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());
     		ful2 = new CACHE("L2 Unified Cache", 2,
                     		KnobL2CacheSize.Value() * KILO,
                     		KnobLineSize.Value(),
                     		KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());
 
  	}
  	if (!KnobDisableLvl3.Value())
  	{
     		// level 2 must be simulated if level 3 is simulated.
     		ASSERTX(!KnobDisableLvl2.Value());
     		// level 3 unified cache
     		ul3 = new CACHE("L3 Unified Cache", 3,
                     		KnobL3CacheSize.Value() * KILO * KnobCoreCount.Value(),
                     		KnobLineSize.Value(),
                     		16, //KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());
     		ful3 = new CACHE("L3 Unified Cache", 3,
                     		KnobL3CacheSize.Value() * KILO * KnobCoreCount.Value(),
                     		KnobLineSize.Value(),
                     		16, //KnobAssociativity.Value(),
                     		KnobSetType.Value(),
                     		KnobWriteMissAllocate.Value(),
                     		KnobSingleThread.Value());
 
  	}
 
  	INS_AddInstrumentFunction(Instruction, 0);
  	PIN_AddFiniFunction(Fini, 0);

  	// Never returns
  	PIN_StartProgram();
  	return 0; // make compiler happy
}
