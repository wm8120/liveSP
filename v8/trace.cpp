#include "trace.hpp"
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;

Trace::Trace()
{
    init();
}

Trace::Trace(string s)
{
    init();
    //line format:
    //pc:inst [; <target addr>[:RegChange:<reg>,<new>,<old> |MemRead:vaddr <addr>,data <value>... |MemWrite:vaddr <addr>,data <value>...]]
    boost::trim(s);
    vector<string> splitVec;
    boost::split(splitVec, s, boost::is_any_of(":"), boost::token_compress_on); 
    pc = strtoull(splitVec[0].c_str(), NULL, 0);
    interpret_opcode(splitVec[1]);
}

void Trace::interpret_opcode(string s)
{
    boost::to_lower(s);
    boost::regex branch_pattern("((c|t)?b)|ret");
    if (boost::regex_search(s, branch_pattern))
        control_flow = true;
    boost::regex sys_exception("svc|hvc|smc|eret");
    if (boost::regex_search(s, sys_exception))
        sys_except = true;
    boost::regex hlt_pattern("^hlt");
    if (boost::regex_search(s, hlt_pattern))
        hlt_exception = true;
}

bool Trace::is_control()
{
    return control_flow || sys_except;
}

bool Trace::is_sys_exception()
{
    return sys_except;
}

bool Trace::is_hlt()
{
    return hlt_exception;
}

void Trace::init()
{
    pc = 0;
    control_flow = false;
    sys_except = false;
    hlt_exception = false;
}

Addr Trace::get_pc()
{
    return pc;
}
