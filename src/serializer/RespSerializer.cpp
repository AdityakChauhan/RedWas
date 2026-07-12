#include "RespSerializer.h"
using namespace std;

string RespSerializer::simpleString(const string &s) {
    return "+" + s + "\r\n";
}

string RespSerializer::bulkString(const string &s) {
    return "$"+to_string(s.length())+"\r\n"+s+"\r\n";
}

string RespSerializer::nullBulk() {
    return "$-1\r\n";
}

string RespSerializer::integer(long long n) {
    return ":"+to_string(n)+"\r\n";
}

string RespSerializer::error(const string &msg) {
    return "-ERR "+msg+"\r\n";
}

string RespSerializer::array(const vector<string>& items)
{
    string response = "*" + to_string(items.size()) + "\r\n";

    for (const auto& item : items)
    {
        response += bulkString(item);
    }

    return response;
}