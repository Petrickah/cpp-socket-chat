#include <iostream>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

class TCPServer {
public:
    TCPServer(const int bufferSize, const int maxPending = 5) : m_ServerBuffSize(bufferSize), m_ServerMaxPending(maxPending) {
        // Create the new server socket using the TCP Protocol
        m_ServerSocketHandle = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (m_ServerSocketHandle < 0) {
            // We close the server with an error if the socket creation fails
            exit(this->CloseServer(1, "Failed to create the socket"));
        }
    }

    ~TCPServer() {
        delete[] m_ServerBuffer;
    }

    int Bind(const uint16_t& serverPort) {
        // Construct the server SockAddr structure
        memset(&m_ClientEchoServer, 0, sizeof(m_ClientEchoServer));
        m_ClientEchoServer.sin_family = AF_INET;
        m_ClientEchoServer.sin_addr.s_addr = htonl(INADDR_ANY);
        m_ClientEchoServer.sin_port = serverPort;

        // Bind the server Socket
        const int enable = 1;
        m_ServerSocketReuseAddr = setsockopt(m_ServerSocketHandle, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        if (m_ServerSocketReuseAddr < 0) {
            return this->CloseServer(1, "Failed to enable ReuseAddr on socket");
        }

        m_ServerSocketBound = bind(m_ServerSocketHandle, (struct sockaddr *) &m_ClientEchoServer, sizeof(m_ClientEchoServer));
        if (m_ServerSocketBound < 0) {
            return this->CloseServer(1, "Failed to bind the server socket");
        }

        // Listen on the server Socket
        m_ServerSocketListen = listen(m_ServerSocketHandle, m_ServerMaxPending);
        if (m_ServerSocketListen < 0) {
            return this->CloseServer(1, "Failed to listen on server socket");
        }

        return 0;
    }

    int ConnectClient() {
        m_ServerClientLength = sizeof(m_ClientEchoServer);
        m_ClientSocketHandle = accept(m_ServerSocketHandle, (struct sockaddr*)&m_ClientEchoServer, &m_ServerClientLength);

        if (m_ClientSocketHandle < 0) {
            return this->CloseServer(1, "Failed to accept client connection");
        }

        std::fprintf(stdout, "Client connected: %s:%d\n", inet_ntoa(m_ClientEchoServer.sin_addr), htons(m_ClientEchoServer.sin_port));
        return HandleClient();
    }

    int CloseServer(const int& errorCode = 0, const char* message = "") {
        close(m_ServerSocketHandle);

        if (errorCode) {
            perror(message);
        }

        return errorCode;
    }

    int CloseClient(const int& errorCode = 0, const char* message = "") {
        close(m_ClientSocketHandle);

        if (errorCode) {
            perror(message);
        }

        return 0;
    }

private:
    int HandleClient() {
        m_ServerBuffer = new char[m_ServerBuffSize];
        m_ServerRecieved = recv(m_ClientSocketHandle, m_ServerBuffer, m_ServerBuffSize, 0);
        
        if (m_ServerRecieved > 0) {
            // Recieve the word from the client
            std::fprintf(stdout, "Recieved: ");
            
            while (m_ServerRecieved > 0) {
                m_ServerBuffer[m_ServerRecieved] = '\0';
                std::fprintf(stdout, "%s\n", m_ServerBuffer);

                if (send(m_ClientSocketHandle, m_ServerBuffer, m_ServerRecieved, 0) != m_ServerRecieved) {
                    return this->CloseClient(1, "Failed to send bytes back to client");
                }

                if (std::strncmp(m_ServerBuffer, "EXIT", m_ServerRecieved) == 0) {
                    return this->CloseClient(1, "The client has been disconected");
                }

                m_ServerRecieved = recv(m_ClientSocketHandle, m_ServerBuffer, m_ServerBuffSize-1, 0);
                if (m_ServerRecieved < 1) {
                    // We close the client with an error if the message wasn't recieved from it
                    return this->CloseClient(1, "Failed to receive bytes from the client");
                }
            }
            
            return this->CloseClient();
        }
        
        return this->CloseClient(1, "Failed to recieve initial bytes from client");
    }

private:
    int m_ServerSocketHandle = 0;
    int m_ServerSocketReuseAddr = 0;
    int m_ServerSocketListen = 0;
    int m_ServerSocketBound = 0;
    int m_ClientSocketHandle = 0;

    int m_ServerRecieved = 0;

    unsigned int m_ServerBuffSize;
    unsigned int m_ServerMaxPending;
    unsigned int m_ServerEchoLength = 0;
    unsigned int m_ServerClientLength = 0;

    char* m_ServerBuffer = nullptr;

    struct sockaddr_in m_ClientEchoServer;
};

// Scaffold placeholder — Task 1.1 (TCP echo listener) replaces this.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "USAGE: ./server <port>\n");
        exit(1);
    }

    const uint16_t& bufferSize = 512;
    TCPServer myServer(bufferSize);
    
    const uint16_t& serverPort = htons(atoi(argv[1]));
    if (myServer.Bind(serverPort) == 0) {
        while (myServer.ConnectClient() == 0);

        return myServer.CloseServer();
    }

    return 1;
}
