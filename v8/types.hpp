#ifndef TYPES
#define TYPES
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <list>

typedef std::uint64_t Addr;
typedef std::pair<Addr, Addr> BasicBlockRange;
typedef std::vector<std::string> StrVec;
typedef std::map<Addr, std::string> PcStrMap;
typedef PcStrMap::iterator PcStrIter;
typedef struct{
    std::string x0;
    std::string ret_pc;
} HLTData;
typedef std::list<HLTData> HLTList;

typedef enum { ZERO = 0, BYTE = 1, HALFWORD = 2, WORD = 4, QUAD = 8} DataStride;
typedef struct{
    DataStride stride;
    std::string data_str;
} RWData;
typedef std::map<Addr, RWData> PcDataMap;
typedef std::map<Addr, RWData>::iterator PcDataIter;
typedef struct{
    std::set<Addr> startpcList;
    Addr freq;
} BBStat;
typedef std::map<Addr, BBStat> PcBBMap;
typedef std::pair<Addr, PcBBMap::iterator> ExitBBPair;

struct BBInfo
{
    /** Unique ID */
    uint64_t id;

    /** Num of static insts in BB */
    uint64_t insts;

    /** Accumulated dynamic inst count executed by BB */
    uint64_t count;
};

namespace std
{
    template <>
    class hash<BasicBlockRange>
    {
        public:
            size_t operator()(const BasicBlockRange &bb) const
            {
                return hash<Addr>()(bb.first + bb.second);
            }
    };
}

#endif
