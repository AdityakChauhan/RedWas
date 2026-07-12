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
#include <condition_variable>

using namespace std;
using Value = variant<string, deque<string>>;

struct Entry
{
    Value value;
    optional<chrono::steady_clock::time_point> expireAt = nullopt;
};

struct StringCommandResult
{
    bool wrongType = false;
    optional<string> value = nullopt;
};

struct VectorCommandResult
{
    bool wrongType = false;
    bool nullValue = false;
    vector<string> values;
};

struct BlockingPopResult
{
    bool wrongType = false;
    bool timedOut = false;
    string key;
    string value;
};

class DataStore
{
private:
    unordered_map<string, Entry> store;
    mutable shared_mutex mutex;
    condition_variable_any listCv;
    thread cleanupThread;
    atomic<bool> stopCleanup = false;

    void cleanupExpiredKeys();
    unordered_map<string, Entry>::iterator findValidIterator(const string &key);
    BlockingPopResult blockingPop(const vector<string> &keys, chrono::milliseconds timeout, bool fromLeft);

public:
    DataStore();
    ~DataStore();

    void set(const string &key, const string &value, optional<chrono::steady_clock::time_point> expireAt = nullopt);

    StringCommandResult get(const string &key);

    bool del(const string &key);

    bool exists(const string &key);

    bool expire(const string &key, chrono::seconds ttl);

    long long ttl(const string &key);

    long long pttl(const string &key);

    bool persist(const string &key);

    optional<size_t> rpush(const string &key, const vector<string> &values);

    optional<size_t> lpush(const string &key, const vector<string> &values);

    optional<size_t> llen(const string &key);

    optional<vector<string>> lrange(const string &key, int l, int r);

    StringCommandResult lpop(const string &key);

    VectorCommandResult lpop(const string &key, size_t count);

    StringCommandResult rpop(const string &key);

    VectorCommandResult rpop(const string &key, size_t count);

    StringCommandResult lindex(const string &key, int index);

    optional<long long> linsert(const string &key, const string &position, const string &pivot, const string &value);

    optional<long long> lrem(const string &key, long long count, const string &value);

    BlockingPopResult blpop(const vector<string> &keys, chrono::milliseconds timeout);

    BlockingPopResult brpop(const vector<string> &keys, chrono::milliseconds timeout);
};
