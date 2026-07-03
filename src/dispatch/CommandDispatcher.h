#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../store/DataStore.h"
using namespace std;

class CommandDispatcher
{
private:
    DataStore &store;
    unordered_map<string, function<string(const vector<string> &)>> handlers;

public:
    string dispatch(const vector<string> &command);

    explicit CommandDispatcher(DataStore &store);
};