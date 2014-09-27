#ifndef UTILS_HH
#define UTILS_HH

#include "pin.H"
#include "XML/XMLParse.h"
#include <set>
#include <ctime>
#include <climits>
#include <cstring>
#include <stdlib.h>
#include <fstream>
#include <cassert>
#include <cstdarg>
#include <sstream>

#define ABSTRACT_CLASS    =0
#define CACHESIM_MAX(a,b) (a>=b) ? a : b
#define CACHESIM_MIN(a,b) (a<=b) ? a : b
#define PAGEBITS          (12)
#define BLOCKBITS         (6) 
#define BLOCKSIZE         (2<<BLOCKBITS) 
#define PAGESIZE          (1<<PAGEBITS)
#define NANO              (1.0e-9)
#define KILO              (1024)
#define MEGA              (KILO*KILO)
#define GIGA              (KILO*MEGA)
#define MAX_CACHE_THREAD  (128) 
#define GETPAGE(addr)     (addr >> PAGEBITS)
#define GETBLOCK(addr)    (addr >> BLOCKBITS)
#define GETSUBBLOCK(addr) ((addr & (PAGESIZE-1)) >> BLOCKBITS)

#define CACHESIM_likely(x)    __builtin_expect (!!(x), 1)
#define CACHESIM_unlikely(x)  __builtin_expect (!!(x), 0)

#define MACHINESIM_PRINT    printf
#define MACHINESIM_FPRINT   fprintf

/* ===================================================================== */
/* General Utilities */
/* ===================================================================== */
BOOL   IsPowerOfTwo(UINT64 n);
INT32  FloorLog2(UINT32 n);
INT32  CeilLog2(UINT32 n);
string StringInt(int number);
string StringDouble(double number);
string StringHex(int number);
string mydecstr(UINT64 v, UINT32 w);

/* ===================================================================== */
/* initialization and finalization prototypes. */
/* ===================================================================== */
VOID main_module_init();
VOID main_module_fini();
VOID instruction_module_init();
VOID instruction_module_fini();
VOID basicblock_module_init();
VOID basicblock_module_fini();
VOID cache_and_tlb_module_init();
VOID cache_and_tlb_module_fini();

/* ===================================================================== */
/* instrumentation function declarations. */
/* ===================================================================== */
VOID ImageInstrument(IMG img, VOID *v);
VOID RoutineInstrument(RTN rtn, VOID *);
VOID InstructionInstrument(INS ins, VOID *v);
VOID SimpleInstructionCount(INS ins, VOID *v);
VOID TraceInstrument(TRACE trace, VOID *v);

/// @ forward class declaration.
class SIMLOWLEVEL;
class SIMLOG;
class SIMPARAMS;
class SIMSTATS;
class SIMLOCK;
class SIMOPTS;
class SIMXLATOR;
class SIMGLOBALS;

/// @ global objects of the simulator.
extern SIMLOWLEVEL  *simaops;
extern SIMPARAMS  *SimWait;
extern SIMSTATS   *SimStats;
extern SIMOPTS    *SimOpts;
extern SIMXLATOR  *SimXlator;
extern SIMGLOBALS *SimTheOne;
extern UINT32     *critsecLevel;


/// @ -------------------------------------------------- @ ///
//  @ Address Space Parser.                              @ ///
/// @ -------------------------------------------------- @ ///

class AddrSpaceMap
{
public:
   typedef struct { UINT64 start; UINT64 end; UINT64 perm; } Region;
private:
   std::vector<Region> regions;
public:
   AddrSpaceMap() { /* do nothing */ }
   virtual ~AddrSpaceMap() { /* do nothing */ }
   /// @ register region.
   void RegisterRegion(UINT64 s, UINT64 e, UINT64 p);
   void PrettyPrint();
};


class AddrSpaceMapParser 
{
private:
   FILE *in;
public:
   /// @ constructor and destructor.
   AddrSpaceMapParser(const char *filename)   {  in = fopen(filename, "r");  }
   virtual ~AddrSpaceMapParser()              {  fclose(in);                 }

   void BreakLine(UINT64 &start, UINT64 &end, UINT64 &perm, char* line);
   /// @ parse the next line until the end of the file.
   bool GetNextRegion(UINT64 &start, UINT64 &end, UINT64 &perm);
};



#define XLOG_LOGLEVELS      6
#define XLOG_LOG_MAX_LENGTH 256

/// @ -------------------------------------------------- @ ///
//  @ log system                                         @ ///
/// @ -------------------------------------------------- @ ///

/// SIMLOG - log system that can adjust what is printed based
/// on level.
class SIMLOG
{
public:
    enum
    {
        FORCELOG=0,   /* force log */
        NOLOG,        /* do not log */
        CRITICAL,     /* only log critical msgs */
        WARNING,      /* log critical and warning msgs */
        VERBOSE,      /* log verbose and upper levels */
        SUPERVERBOSE, /* log superverbose and upper levels */
    };

private:
    /* level of printing and file to print to */
    int level;
    /* file to print to */
    FILE* file;
    /* name of the logger */
    string logname;
    /* print to file as well */
    bool logtofile;
    /* print to stdout as well */
    bool logtostdout;
    /* level names in strings */
    const char* levelname[XLOG_LOGLEVELS];


public:
    /// SIMLOG - constructor/destructor.
    SIMLOG(int lvl, const char* fname, const string lname, bool ltofile, bool ltostdout) :
           level(lvl), logname(lname), logtofile(ltofile), logtostdout(ltostdout)
    {
        levelname[NOLOG]        = "[NOLOG]";
        levelname[CRITICAL]     = "[CRITICAL]";
        levelname[WARNING]      = "[WARNING] ";
        levelname[VERBOSE]      = "[VERBOSE]";
        levelname[FORCELOG]     = "[FORCELOG]";
        levelname[SUPERVERBOSE] = "[SUPERVERBOSE]";

        if (logtofile)
        {
            file = fopen(fname, "w+");
            assert(file);
        }
    }

    /// ~SIMLOG - destructor.
    ~SIMLOG() { if (logtofile) fclose(file); }
    /// logme - log the msg based on the level of the logger and the msg.
    void logme(int loglevel, const string fmt, ...)
    {
        if (loglevel <= level)
        {
            char buffer[XLOG_LOG_MAX_LENGTH];
            va_list args;

            va_start(args, fmt);
            vsnprintf(buffer, sizeof(buffer), fmt.c_str(), args);
            va_end(args);

            string afmt = "[" + logname +  "]" + " "+ levelname[loglevel] + " " +
                          levelname[level]  +  " " + buffer;

            /* where to log */
            if (logtostdout) fprintf(stdout, "%s\n", afmt.c_str());
            if (logtofile)   fprintf(file, "%s\n", afmt.c_str());
        }
    }
};

/// @ IMPORTANT - compile with -lstdc++
/// @ SIMLOWLEVEL - provides ways to do atomic operations on variables.
class SIMLOWLEVEL
{
private:
    /// private ctor for singleton.
    SIMLOWLEVEL() {}
    /// dont forget to declare these two. want to make sure they
    /// are unaccessable otherwise one may accidently get copies of
    /// singleton appearing.
    SIMLOWLEVEL(SIMLOWLEVEL const&);  // don't implement
    void operator=(SIMLOWLEVEL const&);  // don't implement

    /// memory fence.
    inline void membarrier() 
    {
        __asm__ volatile("" ::: "memory"); 
    }
    /// p_atom_int_xadd - add val to the memory location and return the resulting value.
    inline int p_atom_int32_xadd(int *num, int val)
    {
        int value = val;
        __asm__ __volatile__ ("lock; xaddl %%eax, %2;"
                                 : "=a"(value)              //output
                                 : "a" (value), "m" (*num)  //input
                                 : "memory" );
        return value+val;
    }
    inline int64_t p_atom_int64_xadd(int64_t *num, int val)
    {
        int64_t value = val;
        __asm__ __volatile__ ("lock; xaddq %%rax, %2;"
                                 : "=a"(value)              //output
                                 : "a" (value), "m" (*num)  //input
                                 : "memory" );
        return value+val;
    }
    inline uint64_t p_atom_uint64_xadd(uint64_t *num, int val)
    {
        uint64_t value = val;
        __asm__ __volatile__ ("lock; xaddq %%rax, %2;"
                                 : "=a"(value)              //output
                                 : "a" (value), "m" (*num)  //input
                                 : "memory" );
        return value+val;
    }
    inline int p_atom_int32_xchg(int *num, int inval)
    {
        int val = inval;
        __asm__ __volatile__ ( "lock xchg %1,%0"
                                 : "=m" (*num), "=r" (val)
                                 : "1" (inval));
        return val;
    }
    inline int64_t p_atom_int64_xchg(int64_t *num, int64_t inval)
    {
        int64_t val = inval;
        __asm__ __volatile__ ( "lock xchg %1,%0"
                                : "=m" (*num), "=r" (val)
                                : "1" (inval));
        return val;
    }

public:
    inline int atom_int32_inc(int *num)
    {
        return p_atom_int32_xadd(num, 1);
    }
    inline int atom_int32_dec(int *num)
    {
        return p_atom_int32_xadd(num, -1);
    }
    inline int atom_int32_add(int *m, int inval)
    {
        return p_atom_int32_xadd(m, inval);
    }
    inline int atom_int32_sub(int *m, int inval)
    {
        return p_atom_int32_xadd(m, inval);
    }
    inline int64_t atom_int64_inc(int64_t *num)
    {
        return p_atom_int64_xadd(num, 1);
    }
    inline int64_t atom_int64_dec(int64_t *num)
    {
        return p_atom_int64_xadd(num, -1);
    }
    inline uint64_t atom_uint64_inc(uint64_t *num)
    {
        return p_atom_uint64_xadd((uint64_t*)num, 1);
    }
    inline uint64_t atom_uint64_dec(uint64_t *num)
    {
        return p_atom_int64_xadd((int64_t*)num, -1);
    }
    inline int64_t atom_int64_add(int64_t *m, int64_t inval)
    {
        return p_atom_int64_xadd(m, inval);
    }
    inline int64_t atom_int64_sub(int64_t *m, int64_t inval)
    {
        return p_atom_int64_xadd(m, inval);
    }
    int atom_int32_xchg(int *m, int inval)
    {
        return p_atom_int32_xchg(m, inval);
    }
    int atom_int64_xchg(int64_t *m, int64_t inval)
    {
        return p_atom_int64_xchg(m, inval);
    }

    /// get_singleton - only one atomic ops object is needed.
    static SIMLOWLEVEL* get_singleton()
    {
        static SIMLOWLEVEL *at = new SIMLOWLEVEL;
        return at;
    }
};


//// SIMXLATOR - converts int to string or string to int.
class SIMXLATOR
{
private:
    /// private ctor for singleton.
    SIMXLATOR() {}
    /// dont forget to declare these two. want to make sure they
    /// are unaccessable otherwise one may accidently get copies of
    /// singleton appearing.
    SIMXLATOR(SIMXLATOR const&);  // don't implement
    void operator=(SIMXLATOR const&);  // don't implement
public:
    std::string stringify_int(const int64_t num)
    {
       stringstream ss;
       ss << num;
       return ss.str();
    }

    int64_t intify_string(const std::string& str)
    {
       	stringstream ss(str);
	int64_t res;
	return ss >> res ? res : 0;
    }

    /// get_singleton - only one atomic ops object is needed.
    static SIMXLATOR* get_singleton()
    {
        static SIMXLATOR*at = new SIMXLATOR;
        return at;
    }
};


/// SIMSTATS - simulation global stats.
class SIMSTATS
{
private:
    SIMSTATS() : instcount(0) {}
    /// dont forget to declare these two. want to make sure they
    /// are unaccessable otherwise one may accidently get copies of
    /// singleton appearing.
    SIMSTATS(SIMSTATS const&);  // don't implement
    void operator=(SIMSTATS const&);  // don't implement

private:
    UINT64 instcount;    /// global simulated instruction count.
public:
    inline UINT64  get_instcount() const     { return instcount; }
    inline void    set_instcount(UINT64 val) { instcount = val;  }
    inline UINT64* get_instcountaddr()       { return &instcount;}

    /// get_singleton - only one atomic ops object is needed.
    static SIMSTATS* get_singleton()
    {
        static SIMSTATS *at = new SIMSTATS;
        return at;
    }
};

/// SIMPARAMS - simulation enable/disable parameters.
class SIMPARAMS
{
public:
    enum
    {
        WAIT_INSTRUCTION=1,
        WAIT_WORKER_THREAD=2
    };
private:
    UINT64 simulate;
    SIMPARAMS() : simulate(0) {}
    /// dont forget to declare these two. want to make sure they
    /// are unaccessable otherwise one may accidently get copies of
    /// singleton appearing.
    SIMPARAMS(SIMPARAMS const&);  // don't implement
    void operator=(SIMPARAMS const&);  // don't implement

public:
    virtual ~SIMPARAMS() {}
    inline BOOL dosim()           { return simulate == 0;    }
    inline VOID setwait(int wait) { simulate |= (1)<<wait;   }
    inline VOID clrwait(int wait) { simulate &= ~((1)<<wait);}
    /// get_singleton - only one atomic ops object is needed.
    static SIMPARAMS* get_singleton()
    {
        static SIMPARAMS *at = new SIMPARAMS;
        return at;
    }
};

/// SIMLOCKS - simulation locks.
class SIMLOCK
{
private:
    /// provide exclusive access to L3 cache.
    PIN_MUTEX CacheSimMutex;
    SIMLOCK()
    {
        PIN_MutexInit(&CacheSimMutex);
    }
    /// dont forget to declare these two. want to make sure they
    /// are unaccessable otherwise one may accidently get copies of
    /// singleton appearing.
    SIMLOCK(SIMLOCK const&);  // don't implement
    void operator=(SIMLOCK const&);  // don't implement

public:
    virtual ~SIMLOCK() {}
    inline void lock_l3_cache(unsigned tid)  { PIN_MutexLock(&CacheSimMutex);   }
    inline void unlock_l3_cache(unsigned tid){ PIN_MutexUnlock(&CacheSimMutex); }
    static SIMLOCK* get_singleton()
    {
        static SIMLOCK *at = new SIMLOCK;
        return at;
    }
};

/// SIMLOCKS - simulation locks.
class SIMGLOBALS
{
public:
   typedef enum {
      INS_LOAD=0,
      INS_STORE,
      INS_BRANCH,
      INS_CALL,
      INS_RET
   }  INSTYPE;
private:
    // Global instruction count.
    UINT64 InstCount;
    UINT64 FetchCount;
    UINT64 StoreCount;
    UINT64 BranchCount;
    UINT64 CallCount;
    UINT64 ReturnCount;
    UINT64 BasicBlockCount;

public:
    VOID IncInst(void)      { simatom->atom_uint64_inc((UINT64*)&InstCount);        }
    VOID IncFetch(void)     { simatom->atom_uint64_inc((UINT64*)&FetchCount);       }
    VOID IncStore(void)     { simatom->atom_uint64_inc((UINT64*)&StoreCount);       }
    VOID IncBranch(void)    { simatom->atom_uint64_inc((UINT64*)&BranchCount);      }
    VOID IncCall(void)      { simatom->atom_uint64_inc((UINT64*)&CallCount);        }
    VOID IncReturn(void)    { simatom->atom_uint64_inc((UINT64*)&ReturnCount);      }
    VOID IncBasicBlock(void){ simatom->atom_uint64_inc((UINT64*)&BasicBlockCount);  }
private:
    // Global time.
    struct timespec *tinit;
    struct timespec *tfini;
    // logging utility.
    SIMLOG *simlog;
    // used to lock singular objects in the simulator, e.g. LLC
    SIMLOCK *simlock;
    // used to do atomic operations.
    SIMLOWLEVEL *simatom;
private:
    SIMGLOBALS() : InstCount(0), FetchCount(0), StoreCount(0), BranchCount(0), CallCount(0), ReturnCount(0)
    {
       tinit = new struct timespec;
       tfini = new struct timespec;
       *tfini = *tinit = { 0, 0 };
       simlog =  new SIMLOG(SIMLOG::SUPERVERBOSE, "xlog.log", "xx", 1, 1);
       simlock = SIMLOCK::get_singleton();
       simatom = SIMLOWLEVEL::get_singleton();
    }
    /// dont forget to declare these two. want to make sure they
    /// are unaccessable otherwise one may accidently get copies of
    /// singleton appearing.
    SIMGLOBALS(SIMGLOBALS const&);  // don't implement
    void operator=(SIMGLOBALS const&);  // don't implement
public:
    virtual ~SIMGLOBALS() 
    { 
        delete tinit;   tinit   = 0;
        delete tfini;   tfini   = 0;
        delete simlog;  simlog  = 0; 
        delete simlock; simlock = 0;
        delete simatom; simatom = 0;
    }
    
    static SIMGLOBALS* get_singleton()
    {
        static SIMGLOBALS *at = new SIMGLOBALS;
        return at;
    }

    struct timespec *get_time_init()     const  { return tinit;       }
    struct timespec *get_time_fini()     const  { return tfini;       }
    SIMLOG*          get_global_simlog() const  { return simlog;      }
    SIMLOCK*         get_global_simlock()const  { return simlock;     }
    SIMLOWLEVEL*     get_global_simlowl()const  { return simatom;     }
    UINT64           get_global_icount() const  { return InstCount;   }
    UINT64           add_global_icount()        { return simatom->atom_uint64_inc((UINT64*)&InstCount); }
    std::string      StatsInstructionCountLongAll();
};

/// SIMOPTS - simulation options.
class SIMOPTS
{
private:
    // simulation options declarted in hierchical fashion.

    /// cache and tlb shared options.
    string SIM_ReplacePolicy;

    /// miscellaneous simulation options.
    BOOL SIM_TraceRecord;
    BOOL SIM_DetailPageStats;
    BOOL SIM_EnableInsCount;
    BOOL SIM_EnableMemSimul;
    UINT32 SIM_WaitWorkerCount;
    UINT64 SIM_MaxSimInstCount;

private:
    SIMLOG *my_logger;
    ParseXML *SIM_XMLParse;

private:
    void reset_all()
    {
        SIM_TraceRecord     = false;
        SIM_DetailPageStats = false;
        SIM_EnableInsCount = false;
        SIM_EnableMemSimul = false;
        SIM_WaitWorkerCount = 0;
        SIM_MaxSimInstCount = ULLONG_MAX;
    }
 
    SIMOPTS()
    {
        reset_all();
        my_logger = new SIMLOG(SIMLOG::SUPERVERBOSE, "SimOpts.log", "xx", 1, 1);
    }
    /// dont forget to declare these two. want to make sure they
    /// are unaccessable otherwise one may accidently get copies of
    /// singleton appearing.
    SIMOPTS(SIMOPTS const&);  // don't implement
    void operator=(SIMOPTS const&);  // don't implement

public:
    virtual ~SIMOPTS() { delete my_logger; }
    void reset() { reset_all(); }
    void print()  
    {
       std::string optstring;
       optstring += "\nSIM_TraceRecord: "  + SimXlator->stringify_int(SIM_TraceRecord);
       optstring += "\nSIM_MaxSimInstCount: " + SimXlator->stringify_int(SIM_MaxSimInstCount);

       my_logger->logme(SIMLOG::CRITICAL, "simulator options: \n %s", optstring.c_str());
    }

    inline ParseXML *get_xml_parser()           { return SIM_XMLParse;          }
    inline VOID set_xml_parser(ParseXML* p)     { SIM_XMLParse = p;             }
    inline BOOL get_detailpagestats()           { return SIM_DetailPageStats;   }        
    inline VOID get_detailpagestats(BOOL val)   { SIM_DetailPageStats = val;    }        
    inline BOOL get_tracerecord()               { return SIM_TraceRecord;       }        
    inline VOID set_tracerecord(BOOL val)       { SIM_TraceRecord = val;        }        
    inline string get_replacepolicy() const     { return SIM_ReplacePolicy;     }
    inline VOID set_replacepolicy(string val)   { SIM_ReplacePolicy = val;      }
    inline VOID set_workercount(UINT32 val)     { SIM_WaitWorkerCount = val;    }
    inline UINT32 get_workercount(void) const   { return SIM_WaitWorkerCount;   }
    inline VOID set_maxsiminst(UINT64 val)      { SIM_MaxSimInstCount = val;    }
    inline UINT64 get_maxsiminst(void) const    { return SIM_MaxSimInstCount;   }
    inline BOOL get_ins_count(void) const       { return SIM_EnableInsCount;    }
    inline VOID set_ins_count(BOOL val)         { SIM_EnableInsCount = val;     }
    inline BOOL get_mem_simul(void) const       { return SIM_EnableMemSimul;    }
    inline VOID set_mem_simul(BOOL val)         { SIM_EnableMemSimul = val;     }

    //// get_singleton - return the only simulation option in the program.
    static SIMOPTS* get_singleton()
    {
        static SIMOPTS *at = new SIMOPTS;
        return at;
    }
};

/// @ SimInsCount - keep instruction counts.
class SimInsCount 
{
public:
   typedef enum 
   {
      INS_LOAD=0,
      INS_STORE,
      INS_BRANCH,
      INS_CALL,
      INS_RET
   } INSTYPE;
private:
   UINT64 load;
   UINT64 store;
   UINT64 branch;
   UINT64 call;
   UINT64 ret;
public:
   SimInsCount() : load(0), store(0), branch(0), call(0), ret(0) {}
   VOID IncLoad(void)   { ++ load;   }
   VOID IncStore(void)  { ++ store;  }
   VOID IncBranch(void) { ++ branch; }
   VOID IncCall(void)   { ++ call;   }
   VOID IncRet(void)    { ++ ret;    }
   std::string StatsLongAll(); 
};


#endif
