#include "types.hpp"
#include "trace.hpp"

class DetailTrace : public Trace
{
    private:
        string disasm;
        Addr target_pc;
        Addr rw_addr;
        DataStride stride;
        StrVec dataVec;
        string new_x0; //hlt trace excluded

    private:
        bool has_target;
        bool mem_load;
        bool mem_store;
        bool prefetch;
        bool division;
        bool simple;

    public:
        DetailTrace();
        DetailTrace(string s);
        string disassembly();
        bool is_has_target();
        bool is_mem_ld();
        bool is_mem_st();
        bool is_prefetch();
        bool is_division();
        bool is_simple();
        Addr get_target_pc();
        Addr get_rw_addr();
        DataStride get_data_stride();
        StrVec& get_data_vec();
        string& get_x0();

    private:
        void init();
        void interpret_opcode();
};
