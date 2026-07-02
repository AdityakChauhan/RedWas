#include "CommandDispatcher.h"
#include "../serializer/RespSerializer.h"
#include <algorithm> //for transform()
#include <cctype> //for tolower()
using namespace std;

CommandDispatcher::CommandDispatcher() {
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