#include <iostream>
#include <server/Server.h>
using namespace std;

int main() {
    cout<<"RedWas initializing...\n";
    Server server;
    server.start();
    return 0;
}