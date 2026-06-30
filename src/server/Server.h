#pragma once

class Server {
    private:
        int lSocketFD;
        static constexpr int DEFAULT_PORT = 6379;
        static constexpr int DEFAULT_BACKLOG = 128;
        bool createSocket();
        bool configureSocket();
        bool bindSocket();
        bool startListening();
        void acceptConnections();
    public:
        void start();
};