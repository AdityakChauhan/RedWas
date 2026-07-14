#pragma once

#include <string>
#include <vector>
using namespace std;

class RespSerializer {
    public:
        static string simpleString(const string &s);
        static string bulkString(const string &s);
        static string nullBulk();
        static string nullArray();
        static string integer(long long n);
        static string error(const string &msg);
        static string array(const vector<string>& items);
        static string rawArray(const vector<string>& encodedItems);
};

