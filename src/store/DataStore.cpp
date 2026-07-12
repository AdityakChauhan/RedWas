#include "DataStore.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <thread>
using namespace std;

DataStore::DataStore()
{
    cleanupThread = thread(&DataStore::cleanupExpiredKeys, this);
}

DataStore::~DataStore()
{
    stopCleanup = true;
    listCv.notify_all();

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





