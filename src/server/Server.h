#pragma once

#include "../store/DataStore.h"
#include "../dispatch/CommandDispatcher.h"


class Server {
    private:
        DataStore store;
        CommandDispatcher dispatcher;
        int lSocketFD;
        static constexpr int DEFAULT_PORT = 6379;
        static constexpr int DEFAULT_BACKLOG = 128;
        bool createSocket();
        bool configureSocket();
        bool bindSocket();
        bool startListening();
        void acceptConnections();
        void communicate(int clientFD);
    public:
        Server();
        void start();
};