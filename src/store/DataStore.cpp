#include "DataStore.h"
#include <mutex>
#include <chrono>
#include <shared_mutex>
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

optional<string> DataStore::get(const string &key)
{
    unique_lock<shared_mutex> lock(mutex);
    auto it = store.find(key);
    if (it == store.end())
        return nullopt;

    if (it->second.expireAt &&
        chrono::steady_clock::now() >= *it->second.expireAt)
    {
        store.erase(it);
        return nullopt;
    }

    return it->second.value;
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