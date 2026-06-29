#include <sys/socket.h> //for socket, setsockopt
#include "Server.h"
#include <iostream>
#include <netinet/in.h> //for sockaddr_in, htons, INADDR_ANY
#include <unistd.h> //for close
using namespace std;

void Server::start() {
    lSocketFD = socket(AF_INET, SOCK_STREAM, 0); //socket is owned by the process, if process dies -> socket disappears as all fd depending on process are closed by kernel.
    if(lSocketFD == -1) {
        cerr<<"Socket creation failed\n";
        return;
    }
    cout<<"Socket created successfully.\n";
    cout<<"FD: "<<lSocketFD<<endl;
    int opt = 1;
    int SSockOptVal = setsockopt(lSocketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(SSockOptVal==-1) {
        cerr<<"SO_REUSEADDR setup failed.\n";
        return;
    }
    //equivalent to ==0
    cout<<"SO_REUSEADDR enabled successfully.\n";
    sockaddr_in serverAddr{}; //{} initialization value initializes every element to 0, makes it equivalent to memset(&serverAddr,0, sizeof(serverAddr)) as normally local variables are not initialized in c++.
            
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(6379);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); //listen on every interface.

    int bindStatus = bind(lSocketFD, (sockaddr*)&serverAddr, sizeof(serverAddr)); //(sockaddr*) as we may use ipv4, ipv6, unixsocket etc. so we use another parent typeholder
    if(bindStatus == -1) {
        cerr<<"Binding Socket to port failed.\n";
        return;
    }
    cout<<"Socket bound to port 6379 successfully.\n";
    int listenStatus = listen(lSocketFD, 128); // keeping 128 as backlog -> max no of connection requests that can wait before system calls accept().
    if(listenStatus == -1) {
        cerr<<"Listen on Socket failed\n";
        return;
    }
    cout<<"Listen on Socket successfull.\n";

    while(true) { //so that we can accept multiple clients.
        int clientFD = accept(lSocketFD, nullptr, nullptr); //nullptr is client ipv4 address and client ipv4 port - we dont need these so nullptr.

        if(clientFD == -1) {
            cerr<<"Unable to setup a client socket\n";
            continue;
            //dont return, the loop continues.
        }
        cout<<"Client connected successfully.\n";
        cout<<"Client FD is: "<<clientFD<<endl; //The process owns this file descriptor and should close it, when it is done communicating with the client.
        close(clientFD);
        cout<<"Client Disconnected.\n";
    }
}