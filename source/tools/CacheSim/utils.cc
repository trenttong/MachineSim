#include "pin.H"
#include "utils.hh"

#include <set>
#include <climits>
#include <cstring>
#include <stdlib.h>

/* ===================================================================== */
/* General Utilities */
/* ===================================================================== */
string mydecstr(UINT64 v, UINT32 w)
{
    ostringstream o;
    o.width(w);
    o << v;
    string str(o.str());
    return str;
}

BOOL IsPowerOfTwo(UINT64 n)
{
    return ((n & (n - 1)) == 0);
}

INT32 FloorLog2(UINT32 n)
{
    INT32 p = 0;
    if (n == 0) return -1;
    if (n & 0xffff0000) 
    {
        p += 16;
        n >>= 16;
    }
    if (n & 0x0000ff00)	
    {
        p +=  8;
        n >>=  8;
    }
    if (n & 0x000000f0) 
    {
        p +=  4;
        n >>=  4;
    }
    if (n & 0x0000000c) 
    {
        p +=  2;
        n >>=  2;
    }
    if (n & 0x00000002) 
    {
        p +=  1;
    }
    return p;
}

INT32 CeilLog2(UINT32 n)
{
    return FloorLog2(n - 1) + 1;
}

string StringInt(int number)
{
    stringstream ss;
    ss << number;
    return ss.str();
}

string StringDouble(double number)
{
    stringstream ss;
    ss.precision(4);
    ss <<  number;
    return ss.str();
}

string StringHex(int number)
{
    stringstream ss;
    ss << "0x" << std::hex << number;
    return ss.str();
}

void AddrSpaceMap::RegisterRegion(UINT64 s, UINT64 e, UINT64 p)
{
    Region r;
    r.start = s;
    r.end = e;
    r.perm = p;
    regions.push_back(r);
}

void AddrSpaceMap::PrettyPrint()
{
    printf("Initializing printing board ...\n");
    UINT32 ChunkSize = 0, ChunkCount = 0;
    for(std::vector<Region>::iterator I=regions.begin(), E=regions.end(); I!=E; ++I)
    {
        ChunkSize += (*I).end - (*I).start;
        ChunkCount ++;
    }
    printf("average ChunkSize is %d\n", ChunkSize/ChunkCount);
    return;
}

void AddrSpaceMapParser::BreakLine(UINT64 &start, UINT64 &end, UINT64 &perm, char* line)
{
    std::string sline(line);
    std::string sstart = sline.substr(0, sline.find_first_of("-"));
    std::string send   = sline.substr(sline.find_first_of("-")+1, 
                         sline.find_first_of(" ")-sline.find_first_of("-"));
    start = std::stoul(sstart, nullptr, 16);
    end = std::stoul(send, nullptr, 16);
}

bool AddrSpaceMapParser::GetNextRegion(UINT64 &start, UINT64 &end, UINT64 &perm)
{
    char * line = NULL; size_t len = 0;
    if (getline(&line, &len, in) < 0) return false;
    BreakLine(start, end, perm, line); 
    return true;
}

std::string SimInsCount::StatsLongAll()
{
    std::string out;
    out += "Load	" + mydecstr(load, 8);
    out += "\n";
    out += "Store	" + mydecstr(store, 8);
    out += "\n";
    out += "Branch	" + mydecstr(branch, 8);
    out += "\n";
    out += "Call	" + mydecstr(call, 8);
    out += "\n";
    out += "Return	" + mydecstr(ret, 8);
    return out;
}

