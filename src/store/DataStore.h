#pragma once

#include <unordered_map>
#include <string>
#include <shared_mutex>
#include <optional>
using namespace std;

class DataStore {
    private:
    unordered_map<string, string> store;
    mutable shared_mutex mutex;

    public:
    void set(const string &key, const string &value);

    optional<string> get(const string &key) const;

    bool del(const string &key);

    bool exists(const string& key) const;
};