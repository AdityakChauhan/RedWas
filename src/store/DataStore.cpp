#include "DataStore.h"
#include <mutex>
#include <chrono>
#include <shared_mutex>
#include <deque>
using namespace std;

DataStore::DataStore()
{
    cleanupThread = thread(&DataStore::cleanupExpiredKeys, this);
}

DataStore::~DataStore()
{
    stopCleanup = true;

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
            if (it->second.expireAt &&
                now >= *it->second.expireAt)
            {
                it = store.erase(it);
            }
            else
                ++it;
        }
        lock.unlock();

        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void DataStore::set(const string &key, const string &value, optional<chrono::steady_clock::time_point> expireAt)
{
    unique_lock<shared_mutex> lock(mutex);
    store[key] = {value, expireAt};
}

bool DataStore::exists(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = store.find(key);
    if (it == store.end())
    {
        return false;
    }
    if ((it->second.expireAt) && (*it->second.expireAt) <= chrono::steady_clock::now())
    {
        store.erase(it);
        return false;
    }

    return true;
}

bool DataStore::del(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = store.find(key);

    if (it == store.end())
        return false;

    store.erase(it);
    return true;
}

Entry *DataStore::get(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = store.find(key);
    if (it == store.end())
        return nullptr;

    if (it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        return nullptr;
    }

    return &it->second;
}

bool DataStore::expire(const string &key, chrono::seconds ttl)
{
    unique_lock<shared_mutex> lock(mutex);

    auto it = store.find(key);

    if (it == store.end())
        return false;

    if (it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        return false;
    }

    it->second.expireAt = chrono::steady_clock::now() + ttl; // no need to call set and recreate key as it is already present

    return true;
}

long long DataStore::ttl(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);

    auto it = store.find(key);

    if (it == store.end())
        return -2;

    if (!it->second.expireAt)
        return -1;

    auto now = chrono::steady_clock::now();

    if (now >= *it->second.expireAt)
    {
        store.erase(it);
        return -2;
    }

    auto diff = *it->second.expireAt - now;

    return chrono::duration_cast<chrono::seconds>(diff).count();
}

long long DataStore::pttl(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);

    auto it = store.find(key);

    if (it == store.end())
        return -2;

    if (!it->second.expireAt)
        return -1;

    auto now = chrono::steady_clock::now();

    if (now >= *it->second.expireAt)
    {
        store.erase(it);
        return -2;
    }

    auto diff = *it->second.expireAt - now;

    return chrono::duration_cast<chrono::milliseconds>(diff).count();
}

bool DataStore::persist(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);

    auto it = store.find(key);

    if (it == store.end())
        return false;

    auto now = chrono::steady_clock::now();

    if (it->second.expireAt &&
        now >= *it->second.expireAt)
    {
        store.erase(it);
        return false;
    }

    if (!it->second.expireAt)
        return false;

    it->second.expireAt = nullopt;

    return true;
}

optional<size_t> DataStore::rpush(const string &key, const vector<string> &values)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = store.find(key);
    if (it != store.end() &&
        it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        it = store.end();
    }
    if (it == store.end())
    {
        deque<string> list;
        for (const auto &val : values)
        {
            list.push_back(val);
        }
        store[key] = {list, nullopt};
        return list.size();
    }


    if (!holds_alternative<deque<string>>(it->second.value))
        return nullopt;

    auto &list = std::get<deque<string>>(it->second.value);

    for (const auto &val : values)
    {
        list.push_back(val);
    }
    return list.size();
}

optional<size_t> DataStore::lpush(const string &key, const vector<string> &values)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = store.find(key);
    if (it != store.end() &&
        it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        it = store.end();
    }
    if (it == store.end())
    {
        deque<string> list;
        for (const auto &val : values)
        {
            list.push_front(val);
        }
        store[key] = {list, nullopt};
        return list.size();
    }
    
    
    if (!holds_alternative<deque<string>>(it->second.value))
        return nullopt;

    auto &list = std::get<deque<string>>(it->second.value);
    for (const auto &val : values)
    {
        list.push_front(val);
    }

    return list.size();
}

optional<size_t> DataStore::llen(const string& key) {
    unique_lock<shared_mutex> lock(mutex);
    
    auto it = store.find(key);
    if (it != store.end() &&
        it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        it = store.end();
    }

    if (it == store.end()) return 0;
    
    if (!holds_alternative<deque<string>>(it->second.value))
        return nullopt;

    auto &list = std::get<deque<string>>(it->second.value);

    return list.size();
}

optional<vector<string>> DataStore::lrange(const string &key, int l, int r)
{
    unique_lock<shared_mutex> lock(mutex);

    auto it = store.find(key);

    if (it != store.end() &&
        it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        it = store.end();
    }

    if (it == store.end())
        return vector<string>{};

    if (!holds_alternative<deque<string>>(it->second.value))
        return nullopt;

    auto &list = std::get<deque<string>>(it->second.value);

    int n = list.size();

    if (l < 0) l += n;
    if (r < 0) r += n;

    l = max(0, l);
    r = min(n - 1, r);

    if (l > r || l >= n)
        return vector<string>{};

    vector<string> ans;

    for (int i = l; i <= r; i++)
        ans.push_back(list[i]);

    return ans;
}

optional<string> DataStore::lpop(const string& key) {
    unique_lock<shared_mutex> lock(mutex);

    auto it = store.find(key);

    if (it != store.end() &&
        it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        it = store.end();
    }

    if (it == store.end())
        return nullopt;

    if (!holds_alternative<deque<string>>(it->second.value))
        return nullopt;
    
    auto &list = std::get<deque<string>>(it->second.value);

    if(list.empty()) {
        store.erase(it);
        return nullopt;
    }
    auto value = list.front();

    list.pop_front();

    if(list.empty()) {
        store.erase(it);
    }

    return value;
}

optional<string> DataStore::rpop(const string& key) {
    unique_lock<shared_mutex> lock(mutex);

    auto it = store.find(key);

    if (it != store.end() &&
        it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        it = store.end();
    }

    if (it == store.end())
        return nullopt;

    if (!holds_alternative<deque<string>>(it->second.value))
        return nullopt;
    
    auto &list = std::get<deque<string>>(it->second.value);

    if(list.empty()) {
        store.erase(it);
        return nullopt;
    }
    auto value = list.back();

    list.pop_back();

    if(list.empty()) {
        store.erase(it);
    }

    return value;
}