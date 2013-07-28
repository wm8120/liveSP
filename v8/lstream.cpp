#include <iostream>
#include <boost/filesystem.hpp>
#include <sstream>
#include "lstream.hpp"

using namespace std;

LStream::LStream (uint64_t no1, uint64_t no2)
    :cur_file_no(no1),
    max_file_no(no2)
{
    stringstream sstr;
    sstr << "intervals/" << no1;
    string filename = sstr.str();
    if (!boost::filesystem::exists(filename))
    {
        cout << "Warning: the first file doesn't exit. No file will be opened." << endl;
    }
    else
    {
        cur_is.open(filename, ios::in);
    }
}
    

bool LStream::feedline (string& str)
{
    if(getline(cur_is, str))
    {
        return true;
    }
    else
    {
        cur_is.close();
        cur_file_no++;
        if(cur_file_no > max_file_no)
        {
            return false;
        }
        else
        {
            stringstream sstr;
            sstr << "intervals/" << cur_file_no;
            string filename = sstr.str();
            if (!boost::filesystem::exists(filename))
            {
                return false;
            }
            else
            {
                cur_is.open(filename, ios::in);
                if (getline(cur_is, str))
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }
    }
}
