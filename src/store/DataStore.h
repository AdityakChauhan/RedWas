#pragma once

#include <unordered_map>
#include <string>
#include <shared_mutex>
#include <optional>
#include <chrono>
#include <thread>
#include <atomic>
using namespace std;

struct Entry
{
    string value;
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
    
    optional<string> get(const string &key);
    
    bool del(const string &key);
    
    bool exists(const string &key);

    bool expire(const string& key, chrono::seconds ttl);

    long long ttl(const string &key);

    long long pttl(const string &key);

    bool persist(const string &key);
};