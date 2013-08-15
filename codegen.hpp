#include <map>
#include <utility>
//section header information
struct SHInfo
{
    uint64_t vma;
    std::string name;
    uint64_t size;
    std::string attr;
    std::vector<std::pair<uint64_t, std::string>> dataVec;
};

typedef std::vector<std::pair<uint64_t, std::string>> HeapData;
typedef HeapData::iterator MemIter;
typedef std::map<uint64_t, std::string>::iterator CodeIter;
void printData(std::ofstream& fout, MemIter begin, MemIter end);
void printCode(std::ofstream& fout, CodeIter begin, CodeIter end);
void printLinker(std::ofstream& lout, std::map<uint64_t, std::string>& lmap, uint64_t entry_pc);

typedef enum{BYTE=1, HALFWORD=2, WORD=4} Stride_t;
inline std::pair<Stride_t, std::string>  getStride(const std::string &str);
inline Stride_t getStride(const CodeIter &it);

struct BBInfo{
    uint64_t bb_start_pc;
    uint64_t freq;
};

struct MemInfo{
    uint64_t addr;
    uint64_t size;
};
typedef std::vector<MemInfo> UsedMem;
void markUsedMem(UsedMem& usedMem, uint64_t start_addr, uint64_t end_addr);
bool mem_addr_compare(const MemInfo& info1, const MemInfo& info2);
uint64_t findSpace(UsedMem& usedMem, uint64_t size);
