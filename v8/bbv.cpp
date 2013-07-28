#include <iostream>
#include <fstream>
#include <utility>
#include <algorithm>
#include <cassert>
#include "types.hpp"
#include "trace.hpp"
#include "lstream.hpp"
#include <boost/filesystem/operations.hpp>

using namespace std;
using namespace boost::filesystem;

int main(int argc, char* argv[])
{
    //arguments
    if (argc !=2)
    {
        cout << "usage: ./bbv [interval size]" << endl;
        return -1;
    }
    const uint64_t interval_size = strtoul(argv[1], NULL, 0);

    uint64_t interval_drift = 0;
    uint64_t current_bbinst_count = 0;
    uint64_t current_interval_count = 0;
    BasicBlockRange current_bb;
    std::unordered_map<BasicBlockRange, BBInfo> bbMap;

    const string filename = "simpoint.bb";
    ofstream fout, fseq;
    fout.open(filename);
    
    //count file numbers in intervals
    Addr file_num=0;
    for (directory_iterator it("intervals"); it != directory_iterator(); ++it)
    {
        file_num++;
    }
    LStream lstream(0,file_num);
    string s;
    while(lstream.feedline(s))
    {
        Trace oneline(s);
        Addr pc = oneline.get_pc();
        if (!current_bbinst_count)
            current_bb.first = pc;
        current_interval_count++;
        current_bbinst_count++;

        if (oneline.is_control())
        {
            current_bb.second = pc;
            auto map_itr = bbMap.find(current_bb);
            if (map_itr == bbMap.end())
            {
                BBInfo info;
                info.id = bbMap.size() + 1;
                info.insts = current_bbinst_count;
                info.count = current_bbinst_count;
                bbMap.insert(make_pair(current_bb, info));
            }
            else
            {
                BBInfo& info = map_itr->second;
                assert(info.insts == current_bbinst_count);
                info.count += current_bbinst_count;
            }
            current_bbinst_count = 0;


            if (current_interval_count + interval_drift >= interval_size)
            {
                vector<pair<uint64_t, uint64_t>> counts;
                for (auto map_itr = bbMap.begin(); map_itr != bbMap.end(); map_itr++)
                {
                    BBInfo& info = map_itr->second;
                    if (info.count != 0)
                    {
                        counts.push_back(make_pair(info.id, info.count));
                        info.count = 0;
                    }
                }
                sort(counts.begin(), counts.end());

                fout << "T";
                for (auto cnt_itr = counts.begin(); cnt_itr != counts.end(); cnt_itr++)
                {
                    fout << ":" << cnt_itr->first
                        << ":" << cnt_itr->second << " ";
                }
                fout << "\n";

                interval_drift = current_interval_count + interval_drift - interval_size;
                current_interval_count = 0;
            }
        }
    }
    fout.close();
    return 0;
}

