#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace std;

struct StreamID
{
    uint64_t milliseconds = 0;
    uint64_t sequence = 0;

    bool operator<(const StreamID &other) const
    {
        if (milliseconds != other.milliseconds)
        {
            return milliseconds < other.milliseconds;
        }
        return sequence < other.sequence;
    }

    bool operator==(const StreamID &other) const
    {
        return milliseconds == other.milliseconds && sequence == other.sequence;
    }
};

struct Stream
{
    map<StreamID, unordered_map<string, string>> entries;
    optional<StreamID> lastId = nullopt;
};

using Value = variant<string, deque<string>, Stream>;

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

struct StreamEntry
{
    StreamID id;
    unordered_map<string, string> fields;
};

struct XAddResult
{
    bool wrongType = false;
    bool invalidId = false;
    bool notGreater = false;
    string id;
};

struct StreamRangeResult
{
    bool wrongType = false;
    bool invalidId = false;
    vector<StreamEntry> entries;
};

struct StreamReadStream
{
    string key;
    vector<StreamEntry> entries;
};

struct StreamReadResult
{
    bool wrongType = false;
    bool invalidId = false;
    bool timedOut = false;
    vector<StreamReadStream> streams;
};

class DataStore
{
private:
    unordered_map<string, Entry> store;
    mutable shared_mutex mutex;
    condition_variable_any listCv;
    condition_variable_any streamCv;
    thread cleanupThread;
    atomic<bool> stopCleanup = false;

    void cleanupExpiredKeys();
    unordered_map<string, Entry>::iterator findValidIterator(const string &key);
    BlockingPopResult blockingPop(const vector<string> &keys, chrono::milliseconds timeout, bool fromLeft);
    StreamReadResult readStreams(const vector<pair<string, string>> &requests, bool resolveDollarIds);

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

    XAddResult xadd(const string &key, const string &idSpec, const vector<pair<string, string>> &fields);

    optional<size_t> xlen(const string &key);

    StreamRangeResult xrange(const string &key, const string &start, const string &end, optional<size_t> count, bool reverse);

    StreamReadResult xread(const vector<pair<string, string>> &requests, optional<chrono::milliseconds> blockTimeout);
};

