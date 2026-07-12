#include "CommandDispatcher.h"
#include "../serializer/RespSerializer.h"
#include <algorithm> //for transform()
#include <cctype>    //for tolower()
#include <chrono>
#include <limits>
#include <optional>
using namespace std;

namespace
{
const string WRONG_TYPE = "WRONGTYPE Operation against a key holding the wrong kind of value";

optional<long long> parseLongLong(const string &value)
{
    try
    {
        size_t pos = 0;
        long long parsed = stoll(value, &pos);
        if (pos != value.size())
        {
            return nullopt;
        }
        return parsed;
    }
    catch (...)
    {
        return nullopt;
    }
}

optional<int> parseInt(const string &value)
{
    auto parsed = parseLongLong(value);
    if (!parsed || *parsed < numeric_limits<int>::min() || *parsed > numeric_limits<int>::max())
    {
        return nullopt;
    }
    return static_cast<int>(*parsed);
}

optional<size_t> parseCount(const string &value)
{
    auto parsed = parseLongLong(value);
    if (!parsed || *parsed < 0)
    {
        return nullopt;
    }
    return static_cast<size_t>(*parsed);
}

optional<chrono::milliseconds> parseTimeout(const string &value)
{
    try
    {
        size_t pos = 0;
        double seconds = stod(value, &pos);
        if (pos != value.size() || seconds < 0)
        {
            return nullopt;
        }
        return chrono::milliseconds(static_cast<long long>(seconds * 1000));
    }
    catch (...)
    {
        return nullopt;
    }
}

vector<string> tailValues(const vector<string> &command, size_t start)
{
    return vector<string>(command.begin() + start, command.end());
}
}

CommandDispatcher::CommandDispatcher(DataStore &dataStore) : store(dataStore)
{
    handlers["PING"] = [](const vector<string> &command)
    {
        if (command.size() == 1)
            return RespSerializer::simpleString("PONG");
        else if (command.size() == 2)
            return RespSerializer::bulkString(command[1]);
        else
        {
            string errMsg = "wrong number of arguments for 'ping' command";
            return RespSerializer::error(errMsg);
        }
    };

    handlers["ECHO"] = [](const vector<string> &command)
    {
        if (command.size() == 2)
        {
            return RespSerializer::bulkString(command[1]);
        }
        else
        {
            string errMsg = "wrong number of arguments for 'echo' command";
            return RespSerializer::error(errMsg);
        }
    };

    handlers["SET"] = [this](const vector<string> &command)
    {
        if (command.size() != 3 && command.size() != 5)
        {
            return RespSerializer::error("wrong number of arguments for 'set' command");
        }
        if (command.size() == 3)
            store.set(command[1], command[2]);
        else
        {
            string option = command[3];
            optional<chrono::steady_clock::time_point> expireAt = nullopt;

            transform(option.begin(), option.end(), option.begin(),
                      [](unsigned char c)
                      {
                          return toupper(c);
                      });
            int ttl;
            try
            {
                ttl = stoi(command[4]);
                if (ttl <= 0)
                {
                    return RespSerializer::error("invalid expire time in 'set' command");
                }
            }
            catch (...)
            {
                return RespSerializer::error("value is not an integer or out of range");
            }
            if (option == "EX")
            {
                expireAt = chrono::steady_clock::now() + chrono::seconds(ttl);
            }
            else if (option == "PX")
            {
                expireAt = chrono::steady_clock::now() + chrono::milliseconds(ttl);
            }
            else
                return RespSerializer::error("syntax error");
            store.set(command[1], command[2], expireAt);
        }
        return RespSerializer::simpleString("OK");
    };

    handlers["GET"] = [this](const vector<string> &command)
    {
        if (command.size() != 2)
        {
            return RespSerializer::error("wrong number of arguments for 'get' command");
        }

        auto result = store.get(command[1]);
        if (result.wrongType)
        {
            return RespSerializer::error(WRONG_TYPE);
        }
        if (!result.value)
        {
            return RespSerializer::nullBulk();
        }

        return RespSerializer::bulkString(*result.value);
    };

    handlers["DEL"] = [this](const vector<string> &command)
    {
        if (command.size() != 2)
        {
            return RespSerializer::error("wrong number of arguments for 'del' command");
        }

        return RespSerializer::integer(store.del(command[1]) ? 1 : 0);
    };

    handlers["EXISTS"] = [this](const vector<string> &command)
    {
        if (command.size() != 2)
        {
            return RespSerializer::error("wrong number of arguments for 'exists' command");
        }

        return RespSerializer::integer(store.exists(command[1]) ? 1 : 0);
    };

    handlers["EXPIRE"] = [this](const vector<string> &command)
    {
        if (command.size() != 3)
        {
            return RespSerializer::error("wrong number of arguments for 'expire' command");
        }

        int ttl;
        try
        {
            ttl = stoi(command[2]);
            if (ttl <= 0)
            {
                return RespSerializer::error("invalid expire time in 'expire' command");
            }
        }
        catch (...)
        {
            return RespSerializer::error("value is not an integer or out of range");
        }

        if (store.expire(command[1], chrono::seconds(ttl)))
            return RespSerializer::integer(1);
        return RespSerializer::integer(0);
    };

    handlers["TTL"] = [this](const vector<string> &command)
    {
        if (command.size() != 2)
            return RespSerializer::error("wrong number of arguments for 'ttl' command");

        return RespSerializer::integer(store.ttl(command[1]));
    };

    handlers["PTTL"] = [this](const vector<string> &command)
    {
        if (command.size() != 2)
            return RespSerializer::error("wrong number of arguments for 'pttl' command");

        return RespSerializer::integer(store.pttl(command[1]));
    };

    handlers["PERSIST"] = [this](const vector<string> &command)
    {
        if (command.size() != 2)
            return RespSerializer::error("wrong number of arguments for 'persist' command");

        return RespSerializer::integer(store.persist(command[1]) ? 1 : 0);
    };

    handlers["RPUSH"] = [this](const vector<string> &command)
    {
        if (command.size() < 3)
        {
            return RespSerializer::error("wrong number of arguments for 'rpush' command");
        }

        auto len = store.rpush(command[1], tailValues(command, 2));
        if (!len)
        {
            return RespSerializer::error(WRONG_TYPE);
        }

        return RespSerializer::integer(*len);
    };

    handlers["LPUSH"] = [this](const vector<string> &command)
    {
        if (command.size() < 3)
        {
            return RespSerializer::error("wrong number of arguments for 'lpush' command");
        }

        auto len = store.lpush(command[1], tailValues(command, 2));
        if (!len)
        {
            return RespSerializer::error(WRONG_TYPE);
        }

        return RespSerializer::integer(*len);
    };

    handlers["LLEN"] = [this](const vector<string> &command)
    {
        if (command.size() != 2)
        {
            return RespSerializer::error("wrong number of arguments for 'llen' command");
        }

        auto len = store.llen(command[1]);
        if (!len)
        {
            return RespSerializer::error(WRONG_TYPE);
        }

        return RespSerializer::integer(*len);
    };

    handlers["LRANGE"] = [this](const vector<string> &command)
    {
        if (command.size() != 4)
        {
            return RespSerializer::error("wrong number of arguments for 'lrange' command");
        }

        auto l = parseInt(command[2]);
        auto r = parseInt(command[3]);
        if (!l || !r)
        {
            return RespSerializer::error("value is not an integer or out of range");
        }

        auto range = store.lrange(command[1], *l, *r);
        if (!range)
        {
            return RespSerializer::error(WRONG_TYPE);
        }

        return RespSerializer::array(*range);
    };

    handlers["LPOP"] = [this](const vector<string> &command)
    {
        if (command.size() != 2 && command.size() != 3)
        {
            return RespSerializer::error("wrong number of arguments for 'lpop' command");
        }

        if (command.size() == 2)
        {
            auto result = store.lpop(command[1]);
            if (result.wrongType)
            {
                return RespSerializer::error(WRONG_TYPE);
            }
            if (!result.value)
            {
                return RespSerializer::nullBulk();
            }
            return RespSerializer::bulkString(*result.value);
        }

        auto count = parseCount(command[2]);
        if (!count)
        {
            return RespSerializer::error("value is not an integer or out of range");
        }

        auto result = store.lpop(command[1], *count);
        if (result.wrongType)
        {
            return RespSerializer::error(WRONG_TYPE);
        }
        if (result.nullValue)
        {
            return RespSerializer::nullArray();
        }
        return RespSerializer::array(result.values);
    };

    handlers["RPOP"] = [this](const vector<string> &command)
    {
        if (command.size() != 2 && command.size() != 3)
        {
            return RespSerializer::error("wrong number of arguments for 'rpop' command");
        }

        if (command.size() == 2)
        {
            auto result = store.rpop(command[1]);
            if (result.wrongType)
            {
                return RespSerializer::error(WRONG_TYPE);
            }
            if (!result.value)
            {
                return RespSerializer::nullBulk();
            }
            return RespSerializer::bulkString(*result.value);
        }

        auto count = parseCount(command[2]);
        if (!count)
        {
            return RespSerializer::error("value is not an integer or out of range");
        }

        auto result = store.rpop(command[1], *count);
        if (result.wrongType)
        {
            return RespSerializer::error(WRONG_TYPE);
        }
        if (result.nullValue)
        {
            return RespSerializer::nullArray();
        }
        return RespSerializer::array(result.values);
    };

    handlers["LINDEX"] = [this](const vector<string> &command)
    {
        if (command.size() != 3)
        {
            return RespSerializer::error("wrong number of arguments for 'lindex' command");
        }

        auto index = parseInt(command[2]);
        if (!index)
        {
            return RespSerializer::error("value is not an integer or out of range");
        }

        auto result = store.lindex(command[1], *index);
        if (result.wrongType)
        {
            return RespSerializer::error(WRONG_TYPE);
        }
        if (!result.value)
        {
            return RespSerializer::nullBulk();
        }

        return RespSerializer::bulkString(*result.value);
    };

    handlers["LINSERT"] = [this](const vector<string> &command)
    {
        if (command.size() != 5)
        {
            return RespSerializer::error("wrong number of arguments for 'linsert' command");
        }

        string position = command[2];
        transform(position.begin(), position.end(), position.begin(),
                  [](unsigned char c)
                  {
                      return toupper(c);
                  });

        if (position != "BEFORE" && position != "AFTER")
        {
            return RespSerializer::error("syntax error");
        }

        auto len = store.linsert(command[1], position, command[3], command[4]);
        if (!len)
        {
            return RespSerializer::error(WRONG_TYPE);
        }

        return RespSerializer::integer(*len);
    };

    handlers["LREM"] = [this](const vector<string> &command)
    {
        if (command.size() != 4)
        {
            return RespSerializer::error("wrong number of arguments for 'lrem' command");
        }

        auto count = parseLongLong(command[2]);
        if (!count)
        {
            return RespSerializer::error("value is not an integer or out of range");
        }

        auto removed = store.lrem(command[1], *count, command[3]);
        if (!removed)
        {
            return RespSerializer::error(WRONG_TYPE);
        }

        return RespSerializer::integer(*removed);
    };

    handlers["BLPOP"] = [this](const vector<string> &command)
    {
        if (command.size() < 3)
        {
            return RespSerializer::error("wrong number of arguments for 'blpop' command");
        }

        auto timeout = parseTimeout(command.back());
        if (!timeout)
        {
            return RespSerializer::error("timeout is not a float or out of range");
        }

        vector<string> keys(command.begin() + 1, command.end() - 1);
        auto result = store.blpop(keys, *timeout);
        if (result.wrongType)
        {
            return RespSerializer::error(WRONG_TYPE);
        }
        if (result.timedOut)
        {
            return RespSerializer::nullArray();
        }

        return RespSerializer::array({result.key, result.value});
    };

    handlers["BRPOP"] = [this](const vector<string> &command)
    {
        if (command.size() < 3)
        {
            return RespSerializer::error("wrong number of arguments for 'brpop' command");
        }

        auto timeout = parseTimeout(command.back());
        if (!timeout)
        {
            return RespSerializer::error("timeout is not a float or out of range");
        }

        vector<string> keys(command.begin() + 1, command.end() - 1);
        auto result = store.brpop(keys, *timeout);
        if (result.wrongType)
        {
            return RespSerializer::error(WRONG_TYPE);
        }
        if (result.timedOut)
        {
            return RespSerializer::nullArray();
        }

        return RespSerializer::array({result.key, result.value});
    };
}

string CommandDispatcher::dispatch(const vector<string> &command)
{
    if (command.empty())
    {
        return RespSerializer::error("Empty command sent");
    }
    string commandName(command[0]);

    transform(commandName.begin(), commandName.end(), commandName.begin(),
              [](unsigned char c)
              {
                  return toupper(c);
              });
    auto it = handlers.find(commandName);
    if (it == handlers.end())
    {
        return RespSerializer::error("Unknown command '" + commandName + "'");
    }

    return it->second(command);
}


