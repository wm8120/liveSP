#include <string>
#include "types.hpp"

using std::string;

class Trace
{
    private:
        Addr pc;
        bool control_flow;
        bool sys_except;

    public:
        Trace();
        Trace(string s);
        bool is_control();
        bool is_sys_exception();
        Addr get_pc();

    private:
        void init();
        void interpret_opcode(string s);
};
