#include "types.hpp"
#include "detail_trace.hpp"
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <set>
#include <map>
#include <cassert>
#include <cstdlib>
#include <sstream>
#include <boost/algorithm/string.hpp>

using namespace std;

void printData(fstream& fout, PcDataIter begin, PcDataIter end);
void printOneValue(fstream& fout, RWData& data);
void insertInstTable(PcStrMap& instTable, DetailTrace& dtrace, unordered_set<Addr>& WTlist);
void printCode(fstream& fout, PcStrIter begin, PcStrIter end);

int main(int argc, char** argv)
{
    //parse status.txt
    string oneline;
    fstream fcfg;
    fcfg.open("status.txt", ios::in);
    StrVec regs;
    Addr stack_base;
    Addr stack_limit;
    while(getline(fcfg, oneline))
    {
        if(oneline.find("register") != string::npos)
        {
            short i = 0;
            for(; i<32; i++)
            {
                getline(fcfg, oneline);
                boost::trim(oneline);
                regs.push_back(oneline);
            }
        }
        else if(oneline.find("stack") != string::npos)
        {
            getline(fcfg, oneline);
            boost::trim(oneline); 
            size_t pos = oneline.find(",");
            stack_base = strtoull(oneline.substr(0, pos).c_str(), NULL, 16);
            stack_limit = strtoull(oneline.substr(pos+1).c_str(), NULL, 16);
        }
    }
    fcfg.close();
    
    bool bb_start=true;
    Addr start_pc=0;//the pc of first instruction in trace
    Addr cur_bb_startpc;
    //Addr last_bb_start_pc=0;//the pc of first inst of last bb
    //Addr last_bb_exit_pc=0;// the pc of last inst of last bb
    //Addr last_bb_freq=0;// the frequency of last bb
    Addr first_write = 0xffffffffffffffff;
    set<Addr> ignoreList;
    map<Addr, RWData> prepareTable;
    map<Addr, RWData> stackTable;
    map<Addr, string> instTable;
    unordered_set<Addr> WTlist; //list of "WaitTarget" instruction
    PcBBMap bbMap;
    for(Addr i=0; getline(cin, oneline); i++)
    {
        DetailTrace dtrace(oneline);
        Addr cur_pc = dtrace.get_pc();
        if (i==0)
        {
            start_pc = cur_pc;
        }
        if (bb_start)
        {
            cur_bb_startpc = cur_pc;
            //if (cur_pc > last_bb_start_pc)
            //{
            //    last_bb_start_pc = cur_pc;
            //}
            bb_start = false;
        }
        if (dtrace.is_control())
        {
            auto it = bbMap.find(cur_pc);
            if (it != bbMap.end())
            {
                it->second.startpcList.insert(cur_bb_startpc);
                it->second.freq++;
            }
            else
            {
                BBStat bbstat;
                bbstat.startpcList.insert(cur_bb_startpc);
                bbstat.freq = 1;
                bbMap.insert(make_pair(cur_pc, bbstat));
            }
            bb_start = true;
            //if (cur_pc > last_bb_exit_pc)
            //{
            //    last_bb_exit_pc = cur_pc;
            //    last_bb_freq=1;
            //}
            //else if (cur_pc == last_bb_exit_pc)
            //{
            //    last_bb_freq++;
            //}
        }

        Addr maddr = dtrace.get_rw_addr();
        StrVec& datas = dtrace.get_data_vec();
        DataStride stride = dtrace.get_data_stride();
        if (dtrace.is_mem_st())
        {
            if (maddr < first_write)
            {
                first_write = maddr; 
            }
            auto it = datas.begin();
            for (;it != datas.end(); it++)
            {
                if (ignoreList.find(maddr) == ignoreList.end())
                {
                    ignoreList.insert(maddr);
                }
                maddr += stride;
            }
        }
        if (dtrace.is_mem_ld())
        {
            auto it = datas.begin();
            for (;it != datas.end(); it++)
            {
                if (ignoreList.find(maddr) == ignoreList.end())
                {
                    RWData rwd;
                    rwd.stride = stride;
                    rwd.data_str = *it;
                    if ( maddr < stack_base - stack_limit)
                    {
                        prepareTable.insert(make_pair(maddr, rwd));
                    }
                    else if (maddr >= stack_base - stack_limit && maddr < stack_base)
                    {
                        stackTable.insert(make_pair(maddr, rwd));        
                    }
                    else
                    {
                        cerr << "memory usage mismatch configuration" << endl;
                        exit(-1);
                    }
                    ignoreList.insert(maddr);
                }
                maddr += stride;
            }
        }
        auto iit = instTable.find(cur_pc);
        if (iit == instTable.end())
        {
            insertInstTable(instTable, dtrace, WTlist);
        }
        else
        {
            string inst = iit->second;
            if (inst.compare("WaitTarget") == 0)
            {
                WTlist.erase(cur_pc);
                instTable.erase(iit);
                insertInstTable(instTable, dtrace, WTlist);
            }
        }
    }

    ////output synthesis code////
    fstream fout;
    fout.open("synthesis.s", ios::out);
    map<Addr, string> linkMap;
    Addr min_pc = instTable.begin()->first; //min inst pc
    //Addr max_pc = instTable.rbegin()->first;//max inst pc
    Addr sect_startpc = 0;
    auto it = prepareTable.begin();
    PcDataIter sect_startit = it;
    // output data sections
    for(; it != prepareTable.end(); it++)
    {
        Addr pc = it->first;
        if(pc < min_pc)
        {
            if (sect_startpc == 0)
            {
                sect_startit = it;
                sect_startpc = pc;
            }
            else if (pc - sect_startpc >= 0x1000)
            {
                stringstream sstr;
                sstr << ".data" << hex << sect_startpc;
                fout << ".section " << sstr.str() \
                    << ", \"a\"" << endl;
                printData(fout, sect_startit, it);
                linkMap.insert(make_pair(sect_startpc, sstr.str()));
                sect_startit = it;
                sect_startpc = pc;
            }
            else
            {
                continue;
            }
        }
        else if(pc >= min_pc && pc < first_write)
        {
            if (sect_startpc != 0)
            {
                stringstream sstr;
                sstr << ".data" << hex << sect_startpc;
                fout << ".section " << sstr.str() \
                    << ", \"a\"" << endl;
                printData(fout, sect_startit, it);
                linkMap.insert(make_pair(sect_startpc, sstr.str()));
                sect_startpc = 0;
            }
            auto iit = instTable.find(pc);
            if (iit != instTable.end())
            {
                assert(iit->second.compare("WaitTarget")==0);
                instTable.erase(iit);
                WTlist.erase(pc);
            }
            string dir;
            switch(it->second.stride)
            {
                case BYTE:
                    dir = ".byte";
                    break;
                case HALFWORD:
                    dir = ".hword";
                    break;
                case WORD:
                    dir = ".word";
                    break;
                case QUAD:
                    dir = ".quad";
                    break;
                default:
                    cerr << "non-valid data in memory" << endl;
                    exit(-1);
            }
            instTable.insert(make_pair(pc, dir+" "+it->second.data_str));
        }
        else
        {
            if (sect_startpc == 0)
            {
                sect_startit = it;
                sect_startpc = pc; 
            } 
            else if (pc - sect_startpc >= 0x1000)
            {
                stringstream sstr;
                sstr << ".data" << hex << sect_startpc;
                fout << ".section " << sstr.str() \
                    << ", \"aw\"" << endl;
                printData(fout, sect_startit, it);
                linkMap.insert(make_pair(sect_startpc, sstr.str()));
                sect_startit = it;
                sect_startpc = it->first;
            }
            else
            {
                continue;
            }
        }
    }
    if (sect_startpc != 0)
    {
        stringstream sstr;
        sstr << ".data" << hex << sect_startpc;
        fout << ".section " << sstr.str() \
            << ", \"aw\"" << endl;
        printData(fout, sect_startit, it);
        linkMap.insert(make_pair(sect_startpc, sstr.str()));
        sect_startit = it;
        sect_startpc = it->first;
    }

    //replace WaitTarget to nop and insert branch to console at the last bb
    for (auto wt_it = WTlist.begin(); wt_it != WTlist.end(); wt_it++)
    {
        PcStrIter it = instTable.find(*wt_it);
        assert(it->second.compare("WaitTarget") == 0);
        it->second = "nop";
    }

    Addr modified_bb_startpc = 0;
    Addr modified_bb_freq = 0;
    auto bbm_it = bbMap.rbegin();
    auto inst_it = instTable.find(bbm_it->first);
    auto inst_next_it = inst_it;
    advance(inst_next_it, 1);
    while(bbm_it != bbMap.rend())
    {
        if (inst_next_it != instTable.end())
        {
            if (inst_next_it->first - inst_it->first >= 8)
            {
                modified_bb_startpc = *(bbm_it->second.startpcList.rbegin());
                modified_bb_freq = bbm_it->second.freq;
                break;
            }
        }
        bbm_it++;
        inst_it = instTable.find(bbm_it->first);
        inst_next_it = inst_it;
        advance(inst_next_it, 1);
    }
    if (bbm_it == bbMap.rend())
    {
        cerr << "Weired program, synthesis failed" << endl;
        exit(-1);
    }
    string replaced;
    string replacer = "b end";
    inst_it = instTable.find(modified_bb_startpc);
    for (;inst_it != inst_next_it; inst_it++)
    {
        replaced = inst_it->second;
        inst_it->second = replacer;
        replacer = replaced;
    }
    instTable.insert(make_pair(bbm_it->first+4, replacer));
    
    //text sections
    auto iit = instTable.begin();
    sect_startpc = iit->first;
    PcStrIter start_it = instTable.begin();
    for(; iit != instTable.end(); iit++)
    {
        Addr cur_pc = iit->first;
        if (cur_pc-sect_startpc <= 0x1000)
        {
            continue;
        }
        else
        {
            stringstream sstr;
            sstr << ".text" << hex << sect_startpc;
            linkMap.insert(make_pair(sect_startpc, sstr.str()));
            fout << ".section " << sstr.str() << ", \"ax\"" << endl;
            printCode(fout, start_it, iit);
            start_it = iit;
            sect_startpc = iit->first;
        }
    }
    stringstream sstr("");
    sstr << ".text" << hex << sect_startpc;
    linkMap.insert(make_pair(sect_startpc, sstr.str()));
    fout << ".section " << sstr.str() << ", \"ax\"" << endl;
    printCode(fout, start_it, iit);

    //data used for controling store here
    fout << ".section .misc, \"aw\"" << endl;
    fout << "stack_data:" ;
    for (auto it=stackTable.begin(); it != stackTable.end(); it++)
    {
        fout << ".quad 0x" << hex << it->first << endl;
        fout << ".quad " << it->second.data_str << endl;
    }
    Addr regs_cnt = 0;
    for (auto it=regs.begin(); it != regs.end(); it++)
    {
        fout << "register" << dec << regs_cnt << ": .quad " << *it << endl;
        regs_cnt++;
    }
    fout << "max: .quad 0x" << hex << modified_bb_freq+1 << endl;
    fout << "cnt: .quad " << hex << 0 << endl;
    fout << endl;

    //console text
    //fout << ".macro mov64, reg, val\n\
    //    \t movz \\reg, #:abs_g3:\\val\n\
    //    \t movk \\reg, #:abs_g2_nc:\\val\n\
    //    \t movk \\reg, #:abs_g1_nc:\\val\n\
    //    \t movk \\reg, #:abs_g0_nc:\\val\n\
    //    .endm" << endl;

    fout << ".section .console, \"ax\"" << endl;
    fout << ".global start" << endl;
    fout << "start:" << endl;
    //recover stack
    fout << "ldr x0, =stack_data" << endl;
    for( Addr i = 0; i < stackTable.size(); i++)
    {
        fout << "ldr x1, [x0], #8" << endl;
        fout << "ldr x2, [x0], #8" << endl;
        fout << "str x2, [x1]" << endl;
    }
    //recover register
    for( Addr i = 1; i < regs.size(); i++)
    {
        if ( i == 31)
        {
            fout << "ldr x0, =register31" << endl;
            fout << "ldr x0, [x0]" << endl;
            fout << "mov sp, x0" << endl;
        }
        else
        {
            stringstream ss;
            ss << "register" << dec << i;
            fout << "ldr x" << dec << i << ", =" << ss.str() << endl;
            fout << "ldr x" << dec << i << ", [x" << dec << i << "]" << endl;
        }
    }
    fout << "ldr x0, =register0" << endl;
    fout << "ldr x0, [x0]" << endl;

    fout << "b L0x" << hex << start_pc << endl;
    fout << "end: " << endl;
    fout << "stp x1, x2, [sp, #-0x10]" << endl;
    fout << "stp x3, x4, [sp, #-0x20]" << endl;
    fout << "ldr x1, =max" << endl;
    fout << "ldr x2, =cnt" << endl;
    fout << "ldr x3, [x1]" << endl;
    fout << "ldr x4, [x2]" << endl;
    fout << "cmp x3, x4" << endl;
    fout << "beq exit" << endl;
    fout << "add x4, x4, #1" << endl;
    fout << "str x4, [x1]" << endl; 
    fout << "ldp x3, x4, [sp, #-0x20]" << endl;
    fout << "ldp x1, x2, [sp, #-0x10]" << endl;
    fout << "b L0x" << hex << modified_bb_startpc+4 << endl;
    fout << "exit: " << endl;
    //fout << "mov x7, #1" << endl;
    //fout << "mov x0, #42" << endl;
    //fout << "svc #0" << endl;
    fout << "sub sp, sp, #0x10" << endl;
    fout << "movz x1, #0x26" << endl;
    fout << "movk x1, #0x2, lsl #16" << endl;
    fout << "str x1, [sp]" << endl;
    fout << "sxtw x0, w0" << endl;
    fout << "str x0, [sp,#8]" << endl;
    fout << "mov x1, sp" << endl;
    fout << "movz w0, #0x18" << endl;
    fout << "hlt #0xf000" << endl;
    fout << "b ." << endl;

    //linker script
    fstream fscript;
    fscript.open("linker.x", ios::out);
    fscript << "SECTIONS" << endl;
    fscript << "{" << endl;
    //lout << "\tENTRY(L" << hex << entry_pc << ")" << endl;
    fscript << "\tENTRY(start)" << endl;
    bool console_flag=true;
    for ( auto it = linkMap.begin(); it != linkMap.end(); it++)
    {
        if (it->second.find("data") != string::npos && console_flag)
        {
            fscript << "\t.console : {*(.console)}" << endl;
            console_flag = false;
        }
        fscript << "\t. = 0x"<< hex << it->first << ";\n";
        fscript << "\t" << it->second << " : " << "{*(" << it->second << ")}\n";
    }
    fscript << "\t.misc : {*(.misc)}\n" << endl;
    fscript << "}" << endl;

    fout.close();
    fscript.close();
    return 0;
}

void insertInstTable(map<Addr, string>& instTable, DetailTrace& dtrace, unordered_set<Addr>& WTlist)
{
    Addr cur_pc = dtrace.get_pc();
    string inst = dtrace.disassembly();
    instTable.insert(make_pair(cur_pc, inst));
    if (dtrace.is_has_target())
    {
        Addr tgt_pc = dtrace.get_target_pc(); 
        if (instTable.find(tgt_pc) == instTable.end())
        {
            auto result_pair = instTable.insert(make_pair(tgt_pc, "WaitTarget"));
            WTlist.insert(tgt_pc);
        }
    }
}

void printData(fstream& fout, PcDataIter begin, PcDataIter end)
{
    if (begin != end)
    {
        auto it1 = begin;
        auto it2 = it1;
        advance (it2, 1);
        for (; it2 != end; it1++, it2++)
        {
            printOneValue(fout, it1->second);
            DataStride stride = it1->second.stride;
            Addr delta = it2->first - it1->first - stride;
            switch (delta)
            {
                case 1:
                    fout << ".byte 0" << endl;
                    break;
                case 2:
                    fout << ".hword 0" << endl;
                    break;
                case 3:
                    fout << ".hword 0" << endl;
                    fout << ".byte 0" << endl;
                    break;
                default:
                    for (uint64_t i=0; i< delta/4; i++)
                    {
                        fout << ".word 0" << endl;
                    }
                    for (short i=0; i< delta % 4; i++)
                    {
                        fout << ".byte 0" << endl;
                    }
                    break;
            }
        }
        printOneValue(fout, it1->second);

        fout << endl;
    }
}

void printOneValue(fstream& fout, RWData& data)
{
    switch(data.stride)
    {
        case 1:
            fout << ".byte " << data.data_str << endl;
            break;
        case 2:
            fout << ".hword " << data.data_str << endl;
            break;
        case 4:
            fout << ".word " << data.data_str << endl;
            break;
        case 8:
            fout << ".quad " << data.data_str << endl;
            break;
        default:
            cerr << "non-valid data in memory" << endl;
            exit(-1);
    }
}

void printCode(fstream& fout, PcStrIter begin, PcStrIter end)
{
    if (begin != end)
    {
        auto it1 = begin;
        auto it2 = it1;
        advance (it2, 1);
        for (;it2 != end; it1++, it2++)
        {
            fout << "L0x" << hex << it1->first << ": ";
            fout << it1 -> second << endl;
            DataStride stride;
            string inst = it1 -> second;
            if (inst.find(".byte") != string::npos)
            {
                stride = BYTE;
            }
            else if (inst.find(".hword") != string::npos)
            {
                stride = HALFWORD;
            }
            else if (inst.find(".quad") != string::npos)
            {
                stride = QUAD;
            }
            else
            {
                stride = WORD;
            }

            uint64_t delta = it2->first - it1->first;
            assert(delta >= stride);
            delta -= stride;
            switch(delta)
            {
                case 1:
                    fout << ".byte 0" << endl;
                    break;
                case 2:
                    fout << ".hword  0" << endl;
                    break;
                case 3:
                    fout << ".byte 0" << endl;
                    fout << ".hword 0" << endl;
                    break;
                default:
                    for (uint64_t i =0; i < delta/4 ; i++)
                    {
                        fout << "nop" << endl; 
                    }
                    for (uint64_t i=0; i < delta%4 ; i++)
                    {
                        fout << ".byte 0" << endl;
                    }
                    break;
            }
        }
        fout << "L0x" << hex << it1->first << ": ";
        fout << it1 -> second << endl;

        fout << endl;
    }
}