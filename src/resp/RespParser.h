#pragma once

#include <string>
#include <vector>
#include <queue>
using namespace std;


class RespParser {
    private:
        string buffer;
        queue<vector<string>> commands;

        bool tryParse();
        bool readNumber(size_t& pos, int& value);
        bool readBulkString(size_t& pos, std::string& value);
    public:
        bool hasCommand() const;
        void feed(const char* data, size_t len);
        vector<string> nextCommand();
};