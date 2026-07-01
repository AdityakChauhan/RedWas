#include <string>
#include <queue>
#include <cctype> //for isdigit
#include "RespParser.h"
using namespace std;

bool RespParser::hasCommand() const{
    return !commands.empty();
}

bool RespParser::readNumber(size_t& pos, int& value) {
    value = 0;

    while (pos < buffer.size() && buffer[pos] != '\r') {
        if (!isdigit(static_cast<unsigned char>(buffer[pos])))
            return false;

        value = value * 10 + (buffer[pos] - '0');
        pos++;
    }

    if (pos >= buffer.size())
        return false;

    if (pos + 1 >= buffer.size() || buffer[pos] != '\r' || buffer[pos + 1] != '\n')
        return false;

    pos += 2;    
    return true;
}

bool RespParser::readBulkString(size_t& pos, string& value) {
    if (pos >= buffer.size() || buffer[pos] != '$')
        return false;

    pos++;                 

    int length;

    if (!readNumber(pos, length))
        return false;

    if (pos + length + 2 > buffer.size())
        return false;

    value = buffer.substr(pos, length);

    pos += length;

    if (buffer[pos] != '\r' || buffer[pos + 1] != '\n')
        return false;

    pos += 2;

    return true;
}

bool RespParser::tryParse() {
    if (buffer.empty())
        return false;

    if (buffer[0] != '*')
        return false;

    size_t pos = 1;

    int argc;
    if (!readNumber(pos, argc))
        return false;

    vector<string> command;
    command.reserve(argc); //avoids reallocations while doing push_back()

    for (int i = 0; i < argc; i++) {
        string arg;

        if (!readBulkString(pos, arg))
            return false;

        command.push_back(arg);
    }

    commands.push(command);

    buffer.erase(0, pos);

    return true;
}

void RespParser::feed(const char* data, size_t len) {
    buffer.append(data, len);
    while(tryParse());
}

vector<string> RespParser::nextCommand() {
    auto command = commands.front();
    commands.pop();
    return command;
}