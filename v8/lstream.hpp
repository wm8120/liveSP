#include<fstream>
#include<cstdint>

namespace std
{
    class LStream
    {
        private:
            uint64_t cur_file_no;
            uint64_t max_file_no;
            fstream cur_is;

        public:
            LStream (uint64_t no1, uint64_t no2);
            bool feedline (string& str);
    };
}
