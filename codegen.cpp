#include <cstdlib>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include "codegen.hpp"

using namespace std;

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        cout<<"usage: codegen [binary]"<<endl;
        return -1;
    }
    //arguments
    uint64_t stack_bottom = 0xbf000000;
    uint64_t stack_size = 0x00001000;
    uint64_t stack_top = stack_bottom - stack_size;
    uint64_t space_start = 0x00008000;
    string system_cmd = "./arm-objdump -h " + string(argv[1]) + " > tmp_objdump";
    system(system_cmd.c_str());
    
    //section header info
    //store data to corresponding structure
    vector<SHInfo> secHeader; 
    ifstream f;
    f.open("tmp_objdump", ios::in);
    vector<string> splitVec;
    boost::regex e("^\\s*\\d+\\s*[-_\\.a-zA-Z]+\\s*([0-9a-f]{8}\\s*){4}\\s*\\d\\*{2}\\d\\s*$");
    string str;
    while(getline(f, str))
    {
        if(boost::regex_match(str, e))
        {
            splitVec.clear();
            boost::trim(str);
            boost::split(splitVec, str, boost::is_any_of(" "), boost::token_compress_on);
            SHInfo info;
            info.name = splitVec[1];
            info.size = strtoull(splitVec[2].c_str(), NULL, 16);
            uint64_t pc = strtoull(splitVec[3].c_str(), NULL, 16);
            info.vma = pc;
            getline(f, str);
            boost::trim(str);
            info.attr = str;
            secHeader.push_back(info);
        }
    }

    //output data section, if data belong to text section, insert it to instruction map
    ifstream tf;
    tf.open("m5out/synth.tf",ios::in);
    string firstline;
    getline(tf, firstline);//first line of file is memory 
    splitVec.clear();
    boost::split(splitVec, firstline, boost::is_any_of(" "), boost::token_compress_on);
    
    auto sh_it = secHeader.begin();
    auto vec_it = splitVec.begin();
    vec_it = splitVec.erase(vec_it); //skip the first token --- "memory:"
    while (vec_it != splitVec.end())
    {
        size_t equal_pos = vec_it -> find("=");
        string addrstr = vec_it -> substr(0, equal_pos);
        string value_str = vec_it -> substr(equal_pos+1);
        uint64_t addr = strtoull(addrstr.c_str(), NULL, 16);
        bool jmp = true;

        while (sh_it != secHeader.end())
        {
            if(sh_it->vma <= addr && addr < sh_it->vma + sh_it->size)
            {
                jmp = false;
                (sh_it->dataVec).push_back(make_pair(addr, value_str));
                vec_it = splitVec.erase(vec_it);
                break;
            }
            else if (addr >= sh_it->vma + sh_it->size)
            {
                sh_it++;
                continue;
            }
            else if (addr < sh_it->vma && sh_it->vma != 0)
            {
                    cerr << hex << addr << " section vma=" << hex << sh_it->vma << endl;
                    cerr << "memory should be sorted increasely!" << endl;
                    return -1;
            }
            else
            {
                //this memory location and followings doesn't belong to binary file
                break;
            }
        }

        if(jmp)
            break;
    }

    // output .data
    ofstream synthf;
    synthf.open("synthesis.s", ios::out);
    synthf << ".syntax unified" << endl;
    synthf << ".macro mov32, reg, val" << endl;
    synthf << "\tmovw \\reg, #:lower16:\\val" << endl;
    synthf << "\tmovt \\reg, #:upper16:\\val" << endl;
    synthf << ".endm" <<endl;
    ofstream lscript;
    lscript.open("linker.x", ios::out);
    map<uint64_t, string> linkmap;
    map<uint64_t, string> instMap;
    UsedMem usedMem;
    stringstream sstr("");

    for(auto sh_it = secHeader.begin(); sh_it != secHeader.end(); sh_it++)
    {
        if (!((sh_it->dataVec).empty()) )
        {
            if (sh_it->name.compare(".text") == 0)
            {
                auto vec_it = sh_it->dataVec.begin();
                for (;vec_it != sh_it->dataVec.end(); vec_it++)
                {
                    auto p = getStride(vec_it->second);
                    instMap.insert(make_pair(vec_it->first, p.second));
                }
            }
            else
            {
                if (sh_it->name.compare(".bss") == 0)
                {
                    sh_it->name = ".bs";
                    synthf << ".section .bs, \"aw\"" << endl;            
                }
                else
                {
                    synthf << ".section " << sh_it->name << endl;            
                }
                uint64_t memaddr = (sh_it->dataVec.begin())->first;
                linkmap.insert(make_pair(memaddr, sh_it -> name));
                printData(synthf, sh_it->dataVec.begin(), sh_it->dataVec.end());
                markUsedMem(usedMem, sh_it->dataVec.front().first, sh_it->dataVec.back().first);
            }
        }
    }
    secHeader.clear();
    
    //svc emulation
    ifstream fre;
    fre.open("m5out/synth.re", ios::in);
    uint64_t svclines = 0;
    uint64_t svgate_addr=0;
    bool unallocate_svgate = true;
    synthf << ".section .svgate, \"aw\"" << endl;
    synthf << ".word 0x4" << endl;
    svclines++;
    string reline;
    while(getline(fre, reline))
    {
        svclines++;
        boost::trim(reline);
        synthf << ".word 0x" << reline << endl;
    }
    uint64_t svc_size = svclines * 4;

    //stack and heap
    HeapData hpdata;
    vector<pair<uint32_t, string> >stackData;
    for(auto it = splitVec.begin();it != splitVec.end(); it++)
    {
        size_t equal_pos = it->find("=");
        string addr_str = it->substr(0, equal_pos);
        string value_str = it->substr(equal_pos+1);
        uint64_t addr = strtoull(addr_str.c_str(), NULL, 16);

        if (addr >= stack_top && addr < stack_bottom)
        {
            string value = value_str.substr(1);
            stackData.push_back(make_pair(addr, value));
        }
        else
        {
            hpdata.push_back(make_pair(addr, value_str));
        }
    }
    splitVec.clear();
    //output heap data
    uint64_t past_addr;
    auto start_it = hpdata.begin();
    uint64_t start_addr = start_it->first;
    
    for (auto vec_it = start_it; vec_it != hpdata.end(); vec_it++)
    {
        uint64_t addr = vec_it->first;
        if (addr - start_addr <= 0x1000)
        {
            ;
        }
        else
        {
            sstr.str("");
            sstr << ".heapdata" << hex << start_addr ;
            linkmap.insert(make_pair(start_addr, sstr.str()));
            synthf << ".section .heapdata" << hex << start_addr << ", \"aw\"" << endl;
            printData(synthf, start_it, vec_it);
            markUsedMem(usedMem, start_it->first, past_addr);
            start_addr = vec_it -> first;
            start_it = vec_it;
            if (unallocate_svgate && addr - past_addr - 1 > svc_size)
            {
                svgate_addr = past_addr + 1;
                linkmap.insert(make_pair(svgate_addr, ".svgate"));
                unallocate_svgate = false;
            }
        }
        past_addr = addr;
    }
    if (unallocate_svgate)
    {
        cerr << "Too many svc to layout!" << endl;
        exit(-1);
    }
    sstr.str("");
    sstr << ".heapdata" << hex << start_addr ;
    linkmap.insert(make_pair(start_addr, sstr.str()));
    synthf << ".section .heapdata" << start_addr << ", \"aw\"" << endl;
    printData(synthf, start_it, hpdata.end());
    markUsedMem(usedMem, start_it->first, past_addr);
    hpdata.clear();

    //register
    string secondline;
    getline(tf, secondline);
    splitVec.clear();
    boost::split(splitVec, secondline, boost::is_any_of(" "), boost::token_compress_on);
    vector<string> regVec;
    vec_it = splitVec.begin();
    vec_it = splitVec.erase(vec_it); //skip the first token "register:"
    while(vec_it != splitVec.end())
    {
        size_t equal_pos = vec_it->find("="); 
        string s = vec_it->substr(equal_pos+1);
        regVec.push_back(s);
        vec_it++;
    }

    //entry point
    string thirdline;
    getline(tf, thirdline);
    size_t colon_pos = thirdline.find(":");
    string tmpstr = thirdline.substr(colon_pos+1);
    boost::trim(tmpstr);
    uint64_t entry_pc = strtoull(tmpstr.c_str(), NULL, 16);
    
    //recovery text
    string textline;
    while(getline(tf, textline))
    {
        if(textline.find("text:") != string::npos)
            break;
    }
    // the following lines will be code
    vector<string> instWaitVec;
    string line;
    while(getline(tf, line))
    {
        size_t equal_pos = line.find("=");
        string pcstr = line.substr(0, equal_pos);
        string inst = line.substr(equal_pos+1);
        boost::trim(inst);
        uint64_t pc = strtoull(pcstr.c_str(), NULL, 16);
        if (inst.find("WaitInst") != string::npos)
        {
            instWaitVec.push_back(pcstr);
        }
        else
        {
            instMap.insert(make_pair(pc, inst));
        }
    }

    if (!instWaitVec.empty())
    {
        system_cmd = "./arm-objdump -S " + string(argv[1]) + " > tmp_disassembly";
        system(system_cmd.c_str());
        ifstream dumpf; 
        dumpf.open("tmp_disassembly", ios::in);
        string line;
        for(auto vec_it = instWaitVec.begin(); vec_it != instWaitVec.end(); vec_it++)
        {
            stringstream ress;
            ress << "\\s*" << hex << *vec_it << ":";
            boost::regex pattern(ress.str());
            while(getline(dumpf, line))
            {
                if(boost::regex_search(line, pattern))
                {
                    uint64_t pc = strtoull(vec_it->c_str(), NULL, 16);
                    stringstream ss;
                    ss << "\\s*" << hex << *vec_it << ":\\s*[0-9a-f]+\\s*";
                    boost::regex e(ss.str());
                    line = boost::regex_replace(line, e, "");
                    size_t pos = line.find(";");
                    if (pos != string::npos)
                    {
                        line = line.substr(0, pos);
                    }
                    pos = line.find("<");
                    if (pos != string::npos)
                    {
                        line = line.substr(0, pos);
                    }
                    boost::trim(line);

                    instMap.insert(make_pair(pc, line));

                    break;
                }
            }
        }
        dumpf.close();
        system("rm tmp_disassembly");
    }
    instWaitVec.clear();

    // seperate 0xffffxxxx instruction from instMap
    map<uint64_t, string> sysInst;
    if (instMap.find(0xffff0fe0) != instMap.end())
    {
        assert(instMap.find(0xffff0fe4) != instMap.end());
        auto inst_rit = instMap.rbegin();
        sysInst.insert(make_pair(inst_rit->first, inst_rit->second));
        inst_rit++;
        sysInst.insert(make_pair(inst_rit->first, inst_rit->second));
        instMap.erase(0xffff0fe0);
        instMap.erase(0xffff0fe4);
    }


    //pc of bb for counting and frequency
    ifstream fbb;
    fbb.open("m5out/synth.bb", ios::in);
    fbb.seekg(0, fbb.end);
    size_t fbbsize = fbb.tellg();
    fbb.seekg(0, fbb.beg);
    if (fbbsize == 0)
    {
        cerr << "no valid basic block for exiting" << endl;
        exit(-1);
    }

    map<uint64_t, BBInfo> bbAttrMap;
    vector<pair<uint64_t, uint64_t> > freqVec;
    while(getline(fbb, line))
    {
        boost::trim(line);
        boost::split(splitVec, line, boost::is_any_of(" "), boost::token_compress_on);
        BBInfo bb;
        bb.bb_start_pc = strtoull(splitVec[1].c_str(), NULL, 16);
        bb.freq = strtoull(splitVec[2].c_str(), NULL, 16);
        uint64_t pc = strtoull(splitVec[0].c_str(), NULL, 16);
        bbAttrMap.insert(make_pair(pc, bb));
        freqVec.push_back(make_pair(bb.freq, pc));
    }
    splitVec.clear();
    sort(freqVec.begin(), freqVec.end());

    uint64_t modified_bb_start;
    uint64_t modified_bb_exit;
    uint64_t modified_bb_freq;
    uint64_t modified_bb_next;
    auto fv_it = freqVec.begin();
    for (; fv_it != freqVec.end(); fv_it++)
    {
        auto bm_it = bbAttrMap.find(fv_it->second);
        auto in_it = instMap.find(bm_it->first);
        auto in_next_it = in_it;
        advance(in_next_it, 1);
        if (in_next_it != instMap.end())
        {
            if (in_next_it->first - in_it->first >= 8)
            {
                modified_bb_start = bm_it->second.bb_start_pc;
                modified_bb_exit = in_it->first;
                modified_bb_freq = bm_it->second.freq;
                modified_bb_next = in_next_it->first;
                break;
            }
        }
        else
        {
            auto p = instMap.insert(make_pair(in_it->first+8, "nop"));
            assert(p.second);
            in_next_it = p.first;
            modified_bb_start = bm_it->second.bb_start_pc;
            modified_bb_exit = in_it->first;
            modified_bb_freq = bm_it->second.freq;
            modified_bb_next = in_next_it->first;
            break;
        }
    }
    if (fv_it == freqVec.end())
    {
        cerr << "The code layout too dense to synthesize." << endl;
        ofstream debugfile;
        debugfile.open("debugfile.log", ios::out);
        for (auto it=instMap.begin(); it!=instMap.end(); it++)
        {
            debugfile << hex << it->first << ": " << it->second << endl;
        }
        debugfile.close();
        exit(-1);
    }

    string rpler = "b end";
    string rplee;
    for (auto in_it = instMap.find(modified_bb_start); in_it != instMap.find(modified_bb_next); in_it++)
    {
        rplee = in_it->second;
        in_it->second = rpler;
        rpler = rplee;
    }
    instMap.insert(make_pair(modified_bb_exit+4, rpler));


    // output text sections
    auto code_it = instMap.begin();
    uint64_t start_pc = code_it ->first;
    uint64_t past_inst_addr = 0;
    for (auto map_it = instMap.begin(); map_it != instMap.end(); map_it++)
    {
        uint64_t addr = map_it -> first;
        if (addr - start_pc <= 0x1000)
        {
            continue;
        }
        else
        {
            sstr.str("");
            sstr<< ".text" << hex << start_pc ;
            linkmap.insert(make_pair(start_pc, sstr.str()));
            synthf << ".section .text" << hex << start_pc << ", \"ax\"" << endl;
            printCode(synthf, code_it, map_it);
            markUsedMem(usedMem, code_it->first, past_inst_addr);
            code_it = map_it;
            start_pc = map_it -> first;
        }
        past_inst_addr = addr;
    }
    //startname.clear();
    sstr.str("");
    sstr << ".text" << hex << start_pc ;
    linkmap.insert(make_pair(start_pc, sstr.str()));
    synthf << ".section .text" << hex << start_pc << ", \"ax\"" << endl;
    printCode(synthf, code_it, instMap.end());
    markUsedMem(usedMem, code_it->first, past_inst_addr);


    //data used for controling store here
    synthf << ".section .misc, \"aw\"" << endl;
    synthf << "max: .word 0x" << hex << modified_bb_freq+1 << endl;
    synthf << "cnt: .word " << hex << 0 << endl;

    //recovery stack, register and branch to start
    synthf << ".section .console, \"ax\"" << endl;
    synthf << ".global start" << endl;
    synthf << "start: ";
    for (auto it = stackData.begin(); it != stackData.end(); it++)
    {
        synthf << "mov32 r0, 0x"<< hex << it->first << endl;
        synthf << "mov32 r1, 0x" << it->second << endl;
        synthf << "str r1, [r0]" << endl;
    }

    int i=0;
    for (vec_it = regVec.begin(); vec_it != regVec.end(); vec_it++)
    {
        size_t len = vec_it->length();
        if (len <= 4)
        {
            synthf << "movw r" << dec << i << ", 0x" << *vec_it << endl;
            synthf << "movt r" << dec << i << ", 0" << endl;
        }
        else
        {
            synthf << "movw r" << dec << i << ", 0x" << vec_it->substr(len-4) << endl;
            synthf << "movt r" << dec << i << ", 0x" << vec_it->substr(0, len-4) << endl;
        }
        i++;
    }
    synthf << "b L" << hex << entry_pc << endl;
    synthf << "end: " << endl;
    synthf << "push {r0, r1, r3, r4}" << endl;
    synthf << "ldr r0, =max" << endl;
    synthf << "ldr r1, =cnt" << endl;
    synthf << "ldr r3, [r0]" << endl;
    synthf << "ldr r4, [r1]" << endl;
    synthf << "add r4, r4, #1" << endl;
    synthf << "cmp r3, r4" << endl;
    synthf << "beq exit" << endl;
    synthf << "str r4, [r1]" << endl; 
    synthf << "pop {r0, r1, r3, r4}" << endl;
    synthf << "b L" << hex << modified_bb_start+4 << endl;
    synthf << "svc: " << endl;
    synthf << "mov32 r0, 0x" << hex << svgate_addr << endl;
    //synthf << "ldr r0, =svgate" << endl;
    synthf << "ldr r1, [r0]" << endl;
    synthf << "add ip, r1, r0" << endl;
    synthf << "add r1, r1, #20" << endl;
    synthf << "str r1, [r0]" << endl;
    synthf << "ldr r0, [ip], #4" << endl;
    synthf << "ldr r1, [ip], #4" << endl;
    synthf << "ldr r2, [ip], #4" << endl;
    synthf << "ldr r3, [ip]" << endl;
    synthf << "ldr pc, [ip, #4]" << endl;
    if (sysInst.size() != 0)
    {
        for (auto it = sysInst.begin(); it != sysInst.end(); it++)
        {
            synthf << "L" << hex << it->first << ": " << it->second << endl;
        }
    }
    synthf << "exit: " << endl;
    synthf << "mov r7, #1" << endl;
    synthf << "mov r0, #42" << endl;
    synthf << "svc #0" << endl;

    //linker script
    printLinker(lscript, linkmap, entry_pc);

    system("rm tmp_objdump");
    fre.close();
    fbb.close();
    f.close();
    tf.close();
    synthf.close();
    lscript.close();
    return 0;
}

void printData(ofstream& fout, MemIter begin, MemIter end)
{
    if (begin != end)
    {
        auto vec_it1 = begin;
        auto vec_it2 = vec_it1 + 1;
        auto vec_end = end;
        for (; vec_it2 != vec_end; vec_it1++, vec_it2++)
        {
            auto p = getStride(vec_it1->second);
            fout << p.second << endl;
            uint64_t delta = vec_it2->first - vec_it1->first;
            assert( delta >= p.first );
            delta -= p.first;
            switch (delta)
            {
                case 0:
                    break;
                case 1:
                    fout << ".byte 0" << endl;
                    break;
                case 2:
                    fout << ".hword 0" << endl;
                    break;
                default:
                    for (uint64_t i=0; i< delta/4 ; i++)
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
        auto p = getStride(vec_it1->second);
        fout << p.second << endl;
    }
    fout << endl;
}

void printCode(ofstream& fout, CodeIter begin, CodeIter end)
{
    auto it1 = begin;
    auto it2 = it1;
    advance (it2, 1);
    boost::regex syscall("sub\\s*pc,\\s*r0,\\s*#31");
    for (;it2 != end; it1++, it2++)
    {
        fout << "L" << hex << it1->first << ": ";
        if (boost::regex_search(it1->second, syscall))
        {
            fout << "b Lffff0fe0" << endl;
        }
        else if (it1->second.find("svc") != string::npos)
        {
            fout << "b svc" << endl;
        }
        else
        {
            fout << it1 -> second << endl;
        }
        uint64_t delta = it2->first - it1->first;
        Stride_t stride = getStride(it1);
        assert (delta >= stride);
        delta -= stride;
        for (int i=0; i<delta%4; i++)
        {
            fout << ".byte 0" << endl;
        }
        for (uint64_t i =0; i < delta/4; i++)
        {
            fout << "nop" << endl; 
        }
    }
    fout << "L" << hex << it1->first << ": ";
    if (boost::regex_search(it1->second, syscall))
    {
        fout << "b Lffff0fe0" << endl;
    }
    else if (it1->second.find("svc") != string::npos)
    {
        fout << "b svc" << endl;
    }
    else
    {
        fout << it1 -> second << endl;
    }

    fout << endl;
}

void printLinker(ofstream& lout, map<uint64_t, string>& lmap, uint64_t entry_pc)
{
    lout << "SECTIONS" << endl;
    lout << "{" << endl;
    lout << "\tENTRY(start)" << endl;
    lout << "\t.misc : {*(.misc)}\n" << endl;
    lout << "\t.console : {*(.console)}" << endl;
    for ( auto it = lmap.begin(); it != lmap.end(); it++)
    {
        lout << "\t. = 0x"<< hex << it->first << ";\n";
        lout << "\t" << it->second << " : " << "{*(" << it->second << ")}\n";
    }
    lout << "}" << endl;
}

inline pair<Stride_t, std::string>  getStride(const string &str)
{
    Stride_t stride;
    string data;
    if ( str[0] == 'B' )
    {
        data=".byte 0x";
        stride = BYTE;
    }
    else if ( str[0] == 'H' )
    {
        data=".hword 0x";
        stride = HALFWORD;
    }
    else if ( str[0] == 'W' )
    {
        data=".word 0x";
        stride = WORD;
    }
    else
    {
        cerr << "wrong data format: " << str << endl; 
        exit(-1);
    }
    data += str.substr(1);
    return make_pair(stride, data);
}

inline Stride_t getStride(const CodeIter &it)
{
    Stride_t stride;
    string str = it->second;
    if (str.find("byte") != string::npos)
    {
        stride = BYTE;
    }
    else if (str.find("hword") != string::npos)
    {
        stride = HALFWORD;
    }
    else if (str.find("word") != string::npos)
    {
        stride = WORD;
    }
    else
    {
        stride = WORD;
    }
    return stride;
}

void markUsedMem(UsedMem& usedMem, uint64_t start_addr, uint64_t end_addr)
{
    MemInfo minfo;
    minfo.addr = start_addr;
    minfo.size = end_addr - start_addr +1;
    usedMem.push_back(minfo);
}
