#include <sys/socket.h> //for socket, setsockopt
#include "Server.h"
#include <iostream>
#include <netinet/in.h> //for sockaddr_in, htons, INADDR_ANY
#include <unistd.h> //for close
#include <cstring>
#include <thread>
#include "../resp/RespParser.h"
#include "../dispatch/CommandDispatcher.h"
using namespace std;

Server::Server() : dispatcher(store) {}

bool Server::createSocket() {
    lSocketFD = socket(AF_INET, SOCK_STREAM, 0); //socket is owned by the process, if process dies -> socket disappears as all fd depending on process are closed by kernel.
    if(lSocketFD == -1) {
        cerr<<"Socket creation failed\n";
        return false;
    }
    cout<<"Socket created successfully.\n";
    cout<<"FD: "<<lSocketFD<<endl;
    return true;
}

bool Server::configureSocket() {
    int opt = 1;
    int SSockOptVal = setsockopt(lSocketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(SSockOptVal==-1) {
        cerr<<"SO_REUSEADDR setup failed.\n";
        return false;
    }
    //equivalent to ==0
    cout<<"SO_REUSEADDR enabled successfully.\n";
    return true;
}

bool Server::bindSocket() {
    sockaddr_in serverAddr{}; //{} initialization value initializes every element to 0, makes it equivalent to memset(&serverAddr,0, sizeof(serverAddr)) as normally local variables are not initialized in c++.
            
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(DEFAULT_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); //listen on every interface.

    int bindStatus = bind(lSocketFD, (sockaddr*)&serverAddr, sizeof(serverAddr)); //(sockaddr*) as we may use ipv4, ipv6, unixsocket etc. so we use another parent typeholder
    if(bindStatus == -1) {
        cerr<<"Binding Socket to port failed.\n";
        return false;
    }
    cout<<"Socket bound to port "<<DEFAULT_PORT<<" successfully.\n";
    return true;
}

bool Server::startListening() {
    int listenStatus = listen(lSocketFD, DEFAULT_BACKLOG); // keeping 128 as backlog -> max no of connection requests that can wait before system calls accept().
    if(listenStatus == -1) {
        cerr<<"Listen on Socket failed\n";
        return false;
    }
    cout<<"Listen on Socket successfull.\n";
    return true;
}

void Server::acceptConnections() {
    while(true) { //so that we can accept multiple clients.
        int clientFD = accept(lSocketFD, nullptr, nullptr); //nullptr is client ipv4 address and client ipv4 port - we dont need these so nullptr.

        if(clientFD == -1) {
            cerr<<"Unable to setup a client socket\n";
            continue;
            //dont return, the loop continues.
        }
        cout<<"Client connected successfully.\n";
        cout<<"Client FD is: "<<clientFD<<endl; //The process owns this file descriptor and should close it, when it is done communicating with the client.
        thread t(&Server::communicate, this, clientFD);
        t.detach();
    }
}

void Server::communicate(int clientFD) {
    RespParser parser;

    char comBuffer[1024];
    while(true) {
        
        ssize_t recvBytes = recv(clientFD, comBuffer, sizeof(comBuffer),0);

        if(recvBytes==-1) {
            cerr<<"Error occured in receiving communication.\n";
            break;
        } 

        if(recvBytes == 0) {
            cout<<"Client closed connection.\n";
            break;
        }
        parser.feed(comBuffer, recvBytes);
        while(parser.hasCommand()) {
            auto args = parser.nextCommand();

            for(const auto &arg: args) {
                cout<<arg<<" ";
            }
            cout<<endl;
            string response= dispatcher.dispatch(args);
            ssize_t sentBytes = send(clientFD, response.c_str(), response.size(), 0);
            if(sentBytes == -1) {
                cerr<<"Error occured in sending data.\n";
                close(clientFD);
                cout<<"Client Disconnected.\n";
                return;
            }
            else cout<<"Data sent successfully.\n";
        }

        

        
    }
    close(clientFD);
    cout<<"Client Disconnected.\n";
}

void Server::start() {
    if(!createSocket()) return;
    if(!configureSocket()) return;
    if(!bindSocket()) return;
    if(!startListening()) return;
    acceptConnections();
}