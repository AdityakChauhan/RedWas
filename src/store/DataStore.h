#pragma once

#include <unordered_map>
#include <string>
#include <shared_mutex>
#include <optional>
#include <chrono>
#include <thread>
#include <atomic>
#include <variant> 
#include <deque>
#include <vector>

using namespace std;
using Value = variant<string, deque<string>>;

struct Entry
{
    Value value;
    optional<chrono::steady_clock::time_point> expireAt = nullopt;
};

class DataStore
{
private:
    unordered_map<string, Entry> store;
    mutable shared_mutex mutex;
    thread cleanupThread;
    atomic<bool> stopCleanup = false;
    void cleanupExpiredKeys();
    public:
    DataStore();
    ~DataStore();
    
    void set(const string &key, const string &value, optional<chrono::steady_clock::time_point> expireAt = nullopt);
    
    Entry* get(const string &key);
    
    bool del(const string &key);
    
    bool exists(const string &key);

    bool expire(const string& key, chrono::seconds ttl);

    long long ttl(const string &key);

    long long pttl(const string &key);

    bool persist(const string &key);

    optional<size_t> rpush(const string& key, const vector<string>& values);
    
    optional<size_t> lpush(const string& key, const vector<string>& values);

    optional<size_t> llen(const string& key);
    
    optional<vector<string>> lrange(const string& key,const int &l,const int &r);

    optional<string> lpop(const string& key);

    optional<vector<string>> lpop(const string& key, size_t count);

    optional<string> rpop(const string& key);
};