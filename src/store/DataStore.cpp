#include "DataStore.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <thread>
using namespace std;
namespace
{
uint64_t currentTimeMillis()
{
    return static_cast<uint64_t>(chrono::duration_cast<chrono::milliseconds>(
                                     chrono::system_clock::now().time_since_epoch())
                                     .count());
}

string streamIdToString(const StreamID &id)
{
    return to_string(id.milliseconds) + "-" + to_string(id.sequence);
}

bool parseUnsigned(const string &value, uint64_t &out)
{
    if (value.empty())
    {
        return false;
    }

    try
    {
        size_t pos = 0;
        unsigned long long parsed = stoull(value, &pos);
        if (pos != value.size())
        {
            return false;
        }
        out = static_cast<uint64_t>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parseStreamID(const string &value, StreamID &out)
{
    auto dash = value.find('-');
    if (dash == string::npos)
    {
        uint64_t ms;
        if (!parseUnsigned(value, ms))
        {
            return false;
        }
        out = {ms, 0};
        return true;
    }

    uint64_t ms;
    uint64_t seq;
    if (!parseUnsigned(value.substr(0, dash), ms) ||
        !parseUnsigned(value.substr(dash + 1), seq))
    {
        return false;
    }

    out = {ms, seq};
    return true;
}

bool parseRangeBound(const string &value, bool isStart, StreamID &out)
{
    if (value == "-")
    {
        out = {0, 0};
        return true;
    }

    if (value == "+")
    {
        out = {numeric_limits<uint64_t>::max(), numeric_limits<uint64_t>::max()};
        return true;
    }

    auto dash = value.find('-');
    if (dash == string::npos)
    {
        uint64_t ms;
        if (!parseUnsigned(value, ms))
        {
            return false;
        }
        out = {ms, isStart ? 0 : numeric_limits<uint64_t>::max()};
        return true;
    }

    return parseStreamID(value, out);
}

bool isZeroID(const StreamID &id)
{
    return id.milliseconds == 0 && id.sequence == 0;
}

bool lessOrEqual(const StreamID &left, const StreamID &right)
{
    return left < right || left == right;
}
}

DataStore::DataStore()
{
    cleanupThread = thread(&DataStore::cleanupExpiredKeys, this);
}

DataStore::~DataStore()
{
    stopCleanup = true;
    listCv.notify_all();

    streamCv.notify_all();

    if (cleanupThread.joinable())
    {
        cleanupThread.join();
    }
}

void DataStore::cleanupExpiredKeys()
{
    while (!stopCleanup)
    {
        unique_lock<shared_mutex> lock(mutex);
        auto now = chrono::steady_clock::now();
        for (auto it = store.begin(); it != store.end();)
        {
            if (it->second.expireAt && now >= *it->second.expireAt)
            {
                it = store.erase(it);
            }
            else
            {
                ++it;
            }
        }
        lock.unlock();

        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

unordered_map<string, Entry>::iterator DataStore::findValidIterator(const string &key)
{
    auto it = store.find(key);
    if (it != store.end() &&
        it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        it = store.erase(it);
    }
    return it;
}

void DataStore::set(const string &key, const string &value, optional<chrono::steady_clock::time_point> expireAt)
{
    unique_lock<shared_mutex> lock(mutex);
    store[key] = {value, expireAt};
}

StringCommandResult DataStore::get(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return {};
    }

    if (!holds_alternative<string>(it->second.value))
    {
        return {true, nullopt};
    }

    return {false, std::get<string>(it->second.value)};
}

bool DataStore::exists(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    return findValidIterator(key) != store.end();
}

bool DataStore::del(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return false;
    }

    store.erase(it);
    return true;
}

bool DataStore::expire(const string &key, chrono::seconds ttl)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return false;
    }

    it->second.expireAt = chrono::steady_clock::now() + ttl;
    return true;
}

long long DataStore::ttl(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return -2;
    }

    if (!it->second.expireAt)
    {
        return -1;
    }

    auto diff = *it->second.expireAt - chrono::steady_clock::now();
    return chrono::duration_cast<chrono::seconds>(diff).count();
}

long long DataStore::pttl(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return -2;
    }

    if (!it->second.expireAt)
    {
        return -1;
    }

    auto diff = *it->second.expireAt - chrono::steady_clock::now();
    return chrono::duration_cast<chrono::milliseconds>(diff).count();
}

bool DataStore::persist(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end() || !it->second.expireAt)
    {
        return false;
    }

    it->second.expireAt = nullopt;
    return true;
}

optional<size_t> DataStore::rpush(const string &key, const vector<string> &values)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        deque<string> list;
        for (const auto &val : values)
        {
            list.push_back(val);
        }
        store[key] = {list, nullopt};
        listCv.notify_all();
        return list.size();
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return nullopt;
    }

    auto &list = std::get<deque<string>>(it->second.value);
    for (const auto &val : values)
    {
        list.push_back(val);
    }

    listCv.notify_all();
    return list.size();
}

optional<size_t> DataStore::lpush(const string &key, const vector<string> &values)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        deque<string> list;
        for (const auto &val : values)
        {
            list.push_front(val);
        }
        store[key] = {list, nullopt};
        listCv.notify_all();
        return list.size();
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return nullopt;
    }

    auto &list = std::get<deque<string>>(it->second.value);
    for (const auto &val : values)
    {
        list.push_front(val);
    }

    listCv.notify_all();
    return list.size();
}

optional<size_t> DataStore::llen(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return 0;
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return nullopt;
    }

    return std::get<deque<string>>(it->second.value).size();
}

optional<vector<string>> DataStore::lrange(const string &key, int l, int r)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return vector<string>{};
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return nullopt;
    }

    auto &list = std::get<deque<string>>(it->second.value);
    int n = static_cast<int>(list.size());

    if (l < 0)
    {
        l += n;
    }
    if (r < 0)
    {
        r += n;
    }

    l = max(0, l);
    r = min(n - 1, r);

    if (l > r || l >= n)
    {
        return vector<string>{};
    }

    vector<string> ans;
    for (int i = l; i <= r; i++)
    {
        ans.push_back(list[i]);
    }

    return ans;
}

StringCommandResult DataStore::lpop(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return {};
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return {true, nullopt};
    }

    auto &list = std::get<deque<string>>(it->second.value);
    if (list.empty())
    {
        store.erase(it);
        return {};
    }

    string value = list.front();
    list.pop_front();

    if (list.empty())
    {
        store.erase(it);
    }

    return {false, value};
}

VectorCommandResult DataStore::lpop(const string &key, size_t count)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return {false, true, {}};
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return {true, false, {}};
    }

    auto &list = std::get<deque<string>>(it->second.value);
    vector<string> values;

    size_t limit = min(count, list.size());
    for (size_t i = 0; i < limit; i++)
    {
        values.push_back(list.front());
        list.pop_front();
    }

    if (list.empty())
    {
        store.erase(it);
    }

    return {false, false, values};
}

StringCommandResult DataStore::rpop(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return {};
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return {true, nullopt};
    }

    auto &list = std::get<deque<string>>(it->second.value);
    if (list.empty())
    {
        store.erase(it);
        return {};
    }

    string value = list.back();
    list.pop_back();

    if (list.empty())
    {
        store.erase(it);
    }

    return {false, value};
}

VectorCommandResult DataStore::rpop(const string &key, size_t count)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return {false, true, {}};
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return {true, false, {}};
    }

    auto &list = std::get<deque<string>>(it->second.value);
    vector<string> values;

    size_t limit = min(count, list.size());
    for (size_t i = 0; i < limit; i++)
    {
        values.push_back(list.back());
        list.pop_back();
    }

    if (list.empty())
    {
        store.erase(it);
    }

    return {false, false, values};
}

StringCommandResult DataStore::lindex(const string &key, int index)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return {};
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return {true, nullopt};
    }

    auto &list = std::get<deque<string>>(it->second.value);
    int n = static_cast<int>(list.size());

    if (index < 0)
    {
        index += n;
    }

    if (index < 0 || index >= n)
    {
        return {};
    }

    return {false, list[index]};
}

optional<long long> DataStore::linsert(const string &key, const string &position, const string &pivot, const string &value)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return 0;
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return nullopt;
    }

    auto &list = std::get<deque<string>>(it->second.value);
    for (auto iter = list.begin(); iter != list.end(); ++iter)
    {
        if (*iter == pivot)
        {
            if (position == "BEFORE")
            {
                list.insert(iter, value);
            }
            else
            {
                list.insert(next(iter), value);
            }
            return static_cast<long long>(list.size());
        }
    }

    return -1;
}

optional<long long> DataStore::lrem(const string &key, long long count, const string &value)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return 0;
    }

    if (!holds_alternative<deque<string>>(it->second.value))
    {
        return nullopt;
    }

    auto &list = std::get<deque<string>>(it->second.value);
    long long removed = 0;

    if (count >= 0)
    {
        for (auto iter = list.begin(); iter != list.end() && (count == 0 || removed < count);)
        {
            if (*iter == value)
            {
                iter = list.erase(iter);
                removed++;
            }
            else
            {
                ++iter;
            }
        }
    }
    else
    {
        long long limit = -count;
        for (long long i = static_cast<long long>(list.size()) - 1; i >= 0 && removed < limit; i--)
        {
            if (list[static_cast<size_t>(i)] == value)
            {
                list.erase(list.begin() + i);
                removed++;
            }
        }
    }

    if (list.empty())
    {
        store.erase(it);
    }

    return removed;
}

BlockingPopResult DataStore::blockingPop(const vector<string> &keys, chrono::milliseconds timeout, bool fromLeft)
{
    unique_lock<shared_mutex> lock(mutex);
    auto deadline = chrono::steady_clock::now() + timeout;
    bool waitForever = timeout.count() == 0;

    while (!stopCleanup)
    {
        for (const auto &key : keys)
        {
            auto it = findValidIterator(key);
            if (it == store.end())
            {
                continue;
            }

            if (!holds_alternative<deque<string>>(it->second.value))
            {
                return {true, false, "", ""};
            }

            auto &list = std::get<deque<string>>(it->second.value);
            if (list.empty())
            {
                continue;
            }

            string value;
            if (fromLeft)
            {
                value = list.front();
                list.pop_front();
            }
            else
            {
                value = list.back();
                list.pop_back();
            }

            if (list.empty())
            {
                store.erase(it);
            }

            return {false, false, key, value};
        }

        if (waitForever)
        {
            listCv.wait(lock);
        }
        else
        {
            if (listCv.wait_until(lock, deadline) == cv_status::timeout)
            {
                return {false, true, "", ""};
            }
        }
    }

    return {false, true, "", ""};
}

BlockingPopResult DataStore::blpop(const vector<string> &keys, chrono::milliseconds timeout)
{
    return blockingPop(keys, timeout, true);
}

BlockingPopResult DataStore::brpop(const vector<string> &keys, chrono::milliseconds timeout)
{
    return blockingPop(keys, timeout, false);
}






XAddResult DataStore::xadd(const string &key, const string &idSpec, const vector<pair<string, string>> &fields)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it != store.end() && !holds_alternative<Stream>(it->second.value))
    {
        return {true, false, false, ""};
    }

    if (it == store.end())
    {
        store[key] = {Stream{}, nullopt};
        it = store.find(key);
    }

    auto &stream = std::get<Stream>(it->second.value);
    StreamID id;

    if (idSpec == "*")
    {
        id.milliseconds = currentTimeMillis();
        id.sequence = (stream.lastId && stream.lastId->milliseconds == id.milliseconds)
                          ? stream.lastId->sequence + 1
                          : 0;
    }
    else
    {
        auto dash = idSpec.find('-');
        if (dash == string::npos)
        {
            return {false, true, false, ""};
        }

        string msPart = idSpec.substr(0, dash);
        string seqPart = idSpec.substr(dash + 1);
        uint64_t ms;
        if (!parseUnsigned(msPart, ms))
        {
            return {false, true, false, ""};
        }

        id.milliseconds = ms;
        if (seqPart == "*")
        {
            if (stream.lastId && stream.lastId->milliseconds == id.milliseconds)
            {
                id.sequence = stream.lastId->sequence + 1;
            }
            else
            {
                id.sequence = id.milliseconds == 0 ? 1 : 0;
            }
        }
        else if (!parseUnsigned(seqPart, id.sequence))
        {
            return {false, true, false, ""};
        }
    }

    if (isZeroID(id) || (stream.lastId && lessOrEqual(id, *stream.lastId)))
    {
        return {false, false, true, ""};
    }

    unordered_map<string, string> entryFields;
    for (const auto &field : fields)
    {
        entryFields[field.first] = field.second;
    }

    stream.entries[id] = entryFields;
    stream.lastId = id;
    streamCv.notify_all();

    return {false, false, false, streamIdToString(id)};
}

optional<size_t> DataStore::xlen(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return 0;
    }

    if (!holds_alternative<Stream>(it->second.value))
    {
        return nullopt;
    }

    return std::get<Stream>(it->second.value).entries.size();
}

StreamRangeResult DataStore::xrange(const string &key, const string &start, const string &end, optional<size_t> count, bool reverse)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = findValidIterator(key);

    if (it == store.end())
    {
        return {};
    }

    if (!holds_alternative<Stream>(it->second.value))
    {
        return {true, false, {}};
    }

    StreamID startId;
    StreamID endId;
    if (!parseRangeBound(start, true, startId) || !parseRangeBound(end, false, endId))
    {
        return {false, true, {}};
    }

    auto &entries = std::get<Stream>(it->second.value).entries;
    vector<StreamEntry> result;

    if (!reverse)
    {
        for (auto iter = entries.lower_bound(startId); iter != entries.end() && lessOrEqual(iter->first, endId); ++iter)
        {
            result.push_back({iter->first, iter->second});
            if (count && result.size() >= *count)
            {
                break;
            }
        }
    }
    else
    {
        auto iter = entries.upper_bound(endId);
        while (iter != entries.begin())
        {
            --iter;
            if (iter->first < startId)
            {
                break;
            }
            result.push_back({iter->first, iter->second});
            if (count && result.size() >= *count)
            {
                break;
            }
        }
    }

    return {false, false, result};
}

StreamReadResult DataStore::readStreams(const vector<pair<string, string>> &requests, bool resolveDollarIds)
{
    vector<pair<string, StreamID>> parsedRequests;
    parsedRequests.reserve(requests.size());

    for (const auto &request : requests)
    {
        auto it = findValidIterator(request.first);

        if (request.second == "$" && resolveDollarIds)
        {
            if (it != store.end() && !holds_alternative<Stream>(it->second.value))
            {
                return {true, false, false, {}};
            }

            if (it != store.end())
            {
                auto &stream = std::get<Stream>(it->second.value);
                parsedRequests.push_back({request.first, stream.lastId.value_or(StreamID{0, 0})});
            }
            else
            {
                parsedRequests.push_back({request.first, {0, 0}});
            }
            continue;
        }

        StreamID id;
        if (!parseStreamID(request.second, id))
        {
            return {false, true, false, {}};
        }
        parsedRequests.push_back({request.first, id});
    }

    StreamReadResult result;
    for (const auto &request : parsedRequests)
    {
        auto it = findValidIterator(request.first);
        if (it == store.end())
        {
            continue;
        }

        if (!holds_alternative<Stream>(it->second.value))
        {
            return {true, false, false, {}};
        }

        auto &entries = std::get<Stream>(it->second.value).entries;
        vector<StreamEntry> streamEntries;
        for (auto entryIt = entries.upper_bound(request.second); entryIt != entries.end(); ++entryIt)
        {
            streamEntries.push_back({entryIt->first, entryIt->second});
        }

        if (!streamEntries.empty())
        {
            result.streams.push_back({request.first, streamEntries});
        }
    }

    return result;
}

StreamReadResult DataStore::xread(const vector<pair<string, string>> &requests, optional<chrono::milliseconds> blockTimeout)
{
    unique_lock<shared_mutex> lock(mutex);
    vector<pair<string, string>> effectiveRequests = requests;

    auto firstRead = readStreams(effectiveRequests, true);
    if (firstRead.wrongType || firstRead.invalidId || !firstRead.streams.empty() || !blockTimeout)
    {
        return firstRead;
    }

    for (auto &request : effectiveRequests)
    {
        if (request.second != "$")
        {
            continue;
        }

        auto it = findValidIterator(request.first);
        if (it != store.end() && !holds_alternative<Stream>(it->second.value))
        {
            return {true, false, false, {}};
        }

        if (it == store.end())
        {
            request.second = "0-0";
        }
        else
        {
            auto &stream = std::get<Stream>(it->second.value);
            request.second = stream.lastId ? streamIdToString(*stream.lastId) : "0-0";
        }
    }

    bool waitForever = blockTimeout->count() == 0;
    auto deadline = chrono::steady_clock::now() + *blockTimeout;

    while (!stopCleanup)
    {
        if (waitForever)
        {
            streamCv.wait(lock);
        }
        else if (streamCv.wait_until(lock, deadline) == cv_status::timeout)
        {
            return {false, false, true, {}};
        }

        auto result = readStreams(effectiveRequests, false);
        if (result.wrongType || result.invalidId || !result.streams.empty())
        {
            return result;
        }
    }

    return {false, false, true, {}};
}


