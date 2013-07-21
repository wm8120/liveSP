#include "detail_trace.hpp"
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;

DetailTrace::DetailTrace()
    :Trace()
{
   init(); 
}

DetailTrace::DetailTrace(string s)
    :Trace(s)
{
    init();
    //line format:
    //pc:inst [; <target addr>[:RegChange:<reg>,<new>,<old> |MemRead:vaddr <addr>,data <value>... |MemWrite:vaddr <addr>,data <value>...]]
    boost::trim(s);
    vector<string> splitVec;
    boost::split(splitVec, s, boost::is_any_of(":"), boost::token_compress_on); 
    //format instruction
    disasm = boost::to_lower_copy(splitVec[1]);
    if(disasm.find(";") != string::npos)
    {
        has_target = true;
        boost::regex target_pattern("(?<=;[ \\t])0x[0-9,a-f]+");
        boost::smatch result;
        boost::regex_search(s, result, target_pattern);
        string target_label = result[0];
        target_pc = strtoull(target_label.c_str(), NULL, 16);
        target_label = "L"+target_label;
        boost::regex replaced_pattern("\\{pc.*");
        disasm = boost::regex_replace(disasm, replaced_pattern, target_label);
    }

    interpret_opcode();
    if(mem_load || mem_store)
    {
        assert(splitVec.size() == 4);
        string dyninfo = boost::to_lower_copy(splitVec[3]);
        string addr_str = dyninfo.substr(dyninfo.find_first_of("0x"), dyninfo.find(","));
        rw_addr = strtoull(addr_str.c_str(), NULL, 16);
        
        string data_strs = dyninfo.substr(dyninfo.find("data"));
        boost::trim(data_strs);
        boost::split(dataVec, data_strs, boost::is_any_of(" "), boost::token_compress_on); 
        assert(dataVec.size() >= 2);
        dataVec.erase(dataVec.begin());//first element is "data"
        
        auto vec_it = dataVec.begin();
        size_t len = vec_it->length();
        for (;vec_it != dataVec.end(); vec_it++)
        {
            assert( vec_it->length() == len );
        }
        if (len-2 == 16)
        {
            stride = QUAD;
        }
        else
        {
            boost::regex halfword("^\\S+h(\\.[a-z]{2})?\\>");
            boost::regex byte("^\\S+b(\\.[a-z]{2})?\\>");
            if(boost::regex_search(disasm, halfword))
            {
                stride = HALFWORD;
            }
            else if(boost::regex_search(disasm, byte))
            {
                stride = BYTE;
            }
            else
            {
                stride = WORD;
            }
        }
    }

    if (hlt_exception)
    {
        assert(splitVec.size() == 4);
        string reg_change = boost::to_lower_copy(splitVec[3]);
        StrVec strs;
        boost::split(strs, reg_change, boost::is_any_of(","));
        new_x0 = strs[1];
    }
}

string DetailTrace::disassembly()
{
    return disasm;
}

void DetailTrace::interpret_opcode()
{
    string s = disasm;
    boost::regex load_pattern("^ld");
    boost::regex store_pattern("^st");
    boost::regex prefetch_pattern("^prf");
    boost::regex div_pattern("^(s|u|f)div");
    boost::regex hlt_pattern("^hlt");
    boost::regex not_simple("eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al|nv");
    if (boost::regex_search(s, load_pattern))
    {
        mem_load = true;
    }
    else if (boost::regex_search(s, store_pattern))
    {
        mem_store = true;
    }
    else if (boost::regex_search(s, prefetch_pattern))
    {
        prefetch = true;
    }
    else if (boost::regex_search(s, div_pattern))
    {
        division = true;
    }
    else if (boost::regex_search(s, hlt_pattern))
    {
        hlt_exception = true;
    }

    if (boost::regex_search(s, not_simple))
    {
        simple = false;
    }
}

void DetailTrace::init()
{
    target_pc = 0;
    rw_addr = 0;
    stride = ZERO;
    disasm = "";
    has_target = false;
    mem_load = false;
    mem_store = false;
    prefetch = false;
    division = false;
    hlt_exception = false;
    simple = true;
    new_x0 = "";
}

Addr DetailTrace::get_target_pc()
{
    return target_pc;
}

Addr DetailTrace::get_rw_addr()
{
    return rw_addr;
}

DataStride DetailTrace::get_data_stride()
{
    return stride;
}

StrVec& DetailTrace::get_data_vec()
{
    return dataVec;
}

bool DetailTrace::is_has_target()
{
    return has_target;
}

bool DetailTrace::is_mem_ld()
{
    return mem_load;
}

bool DetailTrace::is_mem_st()
{
    return mem_store;
}

bool DetailTrace::is_prefetch()
{
    return prefetch;
}

bool DetailTrace::is_division()
{
    return division;
}

string& DetailTrace::get_x0()
{
    return new_x0;
}

bool DetailTrace::is_hlt()
{
    return hlt_exception;
}

bool DetailTrace::is_simple()
{
    return simple;
}
