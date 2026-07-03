#include "DataStore.h"
#include <mutex>

using namespace std;

void DataStore::set(const string &key, const string &value)
{
    unique_lock<shared_mutex> lock(mutex);
    store[key] = value;
}

bool DataStore::exists(const string &key) const
{
    shared_lock<shared_mutex> lock(mutex);
    bool res = store.find(key) != store.end();
    return res;
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

optional<string> DataStore::get(const string &key) const
{
    shared_lock<shared_mutex> lock(mutex);
    auto it = store.find(key);
    if (it == store.end())
        return nullopt;

    return it->second;
}