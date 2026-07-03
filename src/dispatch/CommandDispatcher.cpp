#include "CommandDispatcher.h"
#include "../serializer/RespSerializer.h"
#include <algorithm> //for transform()
#include <cctype> //for tolower()
using namespace std;

CommandDispatcher::CommandDispatcher(DataStore& dataStore) :store(dataStore) {
    handlers["PING"] = [](const vector<string> &command) {
        if(command.size()==1) return RespSerializer::simpleString("PONG");
        else if(command.size()==2) return RespSerializer::bulkString(command[1]);
        else {
            string errMsg = "wrong number of arguments for 'ping' command";
            return RespSerializer::error(errMsg);
        }
    };

    handlers["ECHO"] = [](const vector<string> &command) {
        if(command.size()==2) {
            return RespSerializer::bulkString(command[1]);
        }
        else {
            string errMsg = "wrong number of arguments for 'echo' command";
            return RespSerializer::error(errMsg);
        } 
    };

    handlers["SET"] = [this](const vector<string> &command) {
        if(command.size() != 3) {
            return RespSerializer::error("wrong number of arguments for 'set' command");
        }

        store.set(command[1], command[2]);
        return RespSerializer::simpleString("OK");    
    };
    
    handlers["GET"] = [this](const vector<string> &command) {
        if(command.size()!=2) {
            return RespSerializer::error("wrong number of arguments for 'get' command");
        }
        auto value = store.get(command[1]);
        if(!value) {
            return RespSerializer::nullBulk();
        }
        return RespSerializer::bulkString(*value); //if optional<string> pass it as a * or value.value()
    };

    handlers["DEL"] = [this](const vector<string> &command) {
        if(command.size()!=2) {
            return RespSerializer::error("wrong number of arguments for 'del' command");
        }

        return RespSerializer::integer(store.del(command[1]) ? 1:0);
    };

    handlers["EXISTS"] = [this](const vector<string> &command) {
        if(command.size()!=2) {
            return RespSerializer::error("wrong number of arguments for 'exists' command");
        }

        return RespSerializer::integer(store.exists(command[1]) ? 1:0);

    };
}

string CommandDispatcher::dispatch(const vector<string> &command) {
    if(command.empty()) {
        return RespSerializer::error("Empty command sent");
    }
    string commandName(command[0]);

    transform(commandName.begin(), commandName.end(), commandName.begin(),
              [](unsigned char c) {
                  return toupper(c);
              });
    auto it = handlers.find(commandName);
    if(it == handlers.end()) {
        return RespSerializer::error("Unknown command '"+commandName+"'");
    }

    return it->second(command);
}