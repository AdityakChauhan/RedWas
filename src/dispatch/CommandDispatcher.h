#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

class CommandDispatcher
{
private:
    unordered_map<string, function<string(const vector<string> &)>> handlers;

public:
    CommandDispatcher();

    string dispatch(const vector<string> &command);
};