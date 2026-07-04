#include "CommandDispatcher.h"
#include "../serializer/RespSerializer.h"
#include <algorithm> //for transform()
#include <cctype>    //for tolower()
#include <chrono>
using namespace std;

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
        auto value = store.get(command[1]);
        if (!value)
        {
            return RespSerializer::nullBulk();
        }
        return RespSerializer::bulkString(*value); // if optional<string> pass it as a * or value.value()
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