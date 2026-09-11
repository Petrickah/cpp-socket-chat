#include <iostream>
#include <map>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

class TCPServer {
public:
    TCPServer(const uint16_t serverPort, const int bufferSize, const int maxPending = 5) : m_EchoServerBuffSize(bufferSize), m_EchoServerMaxPending(maxPending)
    {
        m_ServerSocketHandle = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP); // Create the new server socket using the TCP Protocol
        if (m_ServerSocketHandle < 0 && this->CloseServer(1, "Failed to create the server socket"))
        {
            exit(1);
        }

        const int enable = 1;
        m_ServerSocketReuseAddr = setsockopt(m_ServerSocketHandle, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        if (m_ServerSocketReuseAddr < 0 && this->CloseServer(1, "Failed to set reuse address for the server socket"))
        {
            exit(1);
        }
        
        m_ServerSocketLength = create(m_ServerSocketAddress, serverPort);
        m_ServerSocketBound = bind(m_ServerSocketHandle, (struct sockaddr *) &m_ServerSocketAddress, m_ServerSocketLength); // Bind on the server Socket;
        if (m_ServerSocketBound < 0 && this->CloseServer(1, "Failed to bind the server socket"))
        {
            exit(1);
        }
        
        m_ServerSocketListen = listen(m_ServerSocketHandle, m_EchoServerMaxPending); // Listen on the server Socket
        if (m_ServerSocketListen < 0 && this->CloseServer(1, "Failed to listen on server socket"))
        {
            exit(1);
        }

        FD_ZERO(&m_ServerSocketHandleSet);
        FD_SET(m_ServerSocketHandle, &m_ServerSocketHandleSet);
        m_ServerSocketHandleSetSize = m_ServerSocketHandle + 1;
    }

    ~TCPServer() {
        delete[] m_ServerBuffer;
    }

    int Update()
    {
        timeval tv;
        tv.tv_usec = 0.0;
        tv.tv_sec = m_EchoServerMaxPending;

        while (true)
        {
            sockaddr_in clientSocketAddress;
            socklen_t clientSocketLength = sizeof(clientSocketAddress);
            
            int clientSocketHandle = m_ServerSocketHandle + 1;
            if (SelectClient(m_ServerSocketHandleSetSize, m_ServerSocketHandleSet, clientSocketHandle, clientSocketAddress, clientSocketLength, tv))
            {
                if (clientSocketHandle == m_ServerSocketHandle)
                {
                    if (ConnectClient(clientSocketHandle, clientSocketAddress, clientSocketLength))
                    {
                        std::fprintf(stdout, "Client connected: %s:%d\n", inet_ntoa(clientSocketAddress.sin_addr), htons(clientSocketAddress.sin_port));
                        m_EchoServerClients.insert(std::make_pair(clientSocketHandle, clientSocketAddress));

                        FD_SET(clientSocketHandle, &m_ServerSocketHandleSet);

                        if (clientSocketHandle + 1 > m_ServerSocketHandleSetSize)
                        {
                            m_ServerSocketHandleSetSize = clientSocketHandle + 1;
                        }
                    }
                }
                else
                {
                    this->HandleClient(clientSocketHandle, clientSocketAddress, clientSocketLength);
                }
            }
        }

        return this->CloseServer();
    }

private:
    size_t create(sockaddr_in& clientSocketAddress, uint16_t serverPort = 0)
    {
        // Construct the server SockAddr_In structure
        memset(&clientSocketAddress, 0, sizeof(clientSocketAddress));
        clientSocketAddress.sin_family = AF_INET;
        clientSocketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        clientSocketAddress.sin_port = serverPort;

        return sizeof(clientSocketAddress);
    }

    bool ConnectClient(int& clientSocketHandle, sockaddr_in& clientSocketAddress, socklen_t& clientSocketLength)
    {
        clientSocketLength = create(clientSocketAddress);
        clientSocketHandle = accept(m_ServerSocketHandle, (struct sockaddr*)&clientSocketAddress, &clientSocketLength);
        
        return clientSocketHandle >= 0 && m_EchoServerClients.find(clientSocketHandle) == m_EchoServerClients.end();
    }

    bool UpdateSocketHandleSet(fd_set* rfds, int& clientSocketHandle)
    {
        if (FD_ISSET(m_ServerSocketHandle, rfds))
        {
            clientSocketHandle = m_ServerSocketHandle;
            return true;
        }

        std::map<int, struct sockaddr_in>::const_iterator currClientSocketIterator = m_EchoServerClients.begin();
        while (currClientSocketIterator != m_EchoServerClients.end())
        {
            if (FD_ISSET(currClientSocketIterator->first, rfds))
            {
                clientSocketHandle = currClientSocketIterator->first;
                return true;
            }
            
            currClientSocketIterator++;
        }

        return false;
    }

    bool SelectClient(int nfds, fd_set rfds, int &clientSocketHandle, sockaddr_in& clientSocketAddress, socklen_t& clientSocketLength, timeval tv)
    {
        switch (select(nfds, &rfds, NULL, NULL, &tv))
        {
            case (-1):
            {
                // Error - We consider the server is free to choose from the next clients
                return false;
            }
            case (0):
            {
                // Timeout - We consider the server is free to choose from the next clients
                return false;
            }
            default:
            {
                // Accepted - We consider the server is ready to recieve something from a client
                if (UpdateSocketHandleSet(&rfds, clientSocketHandle))
                {
                    std::map<int, struct sockaddr_in>::const_iterator currClientSocketIterator = m_EchoServerClients.find(clientSocketHandle);
                    if (currClientSocketIterator != m_EchoServerClients.end())
                    {
                        clientSocketHandle  = currClientSocketIterator->first;
                        clientSocketAddress = currClientSocketIterator->second;
                        clientSocketLength  = sizeof(clientSocketAddress);
                    }
                    else
                    {
                        clientSocketHandle  = m_ServerSocketHandle;
                        clientSocketAddress = m_ServerSocketAddress;
                        clientSocketLength  = sizeof(clientSocketAddress);
                    }

                    return true;
                }

                return false;
            }
        }
        
        return false;
    }

    bool HandleClient(const int& clientSocketHandle, sockaddr_in& clientSocketAddress, socklen_t& clientSocketLength)
    {
        m_ServerBuffer = new char[m_EchoServerBuffSize];
        m_ServerSocketRecieved = recvfrom(clientSocketHandle, m_ServerBuffer, m_EchoServerBuffSize, 0, (struct sockaddr*) &clientSocketAddress, &clientSocketLength);
        
        if (m_ServerSocketRecieved < 1)
        {
            // We close the client with an warning if the client was closed
            return this->CloseClient(clientSocketHandle, 0, "The client has been disconected");
        }
        else
        {
            m_ServerBuffer[m_ServerSocketRecieved] = '\0';
            std::fprintf(stdout, "Recieved: %s\n", m_ServerBuffer);
    
            if (sendto(clientSocketHandle, m_ServerBuffer, m_ServerSocketRecieved, 0, (struct sockaddr*) &clientSocketAddress, clientSocketLength) != m_ServerSocketRecieved)
            {
                // We close the client with an error if the message couldn't be send
                return this->CloseClient(clientSocketHandle, 1, "Failed to send bytes back to client");
            }
    
            if (std::strncmp(m_ServerBuffer, "EXIT", m_ServerSocketRecieved) == 0)
            {
                // We close the client with an warning if the message recieved was EXIT
                return this->CloseClient(clientSocketHandle, 0, "The client has been disconected");
            }
        }

        return true;
    }

    bool CloseClient(int clientSocketHandle, const int& errorCode = 0, const char* message = "")
    {
        std::map<int, struct sockaddr_in>::const_iterator currClientSocketIterator = m_EchoServerClients.find(clientSocketHandle);
        if (currClientSocketIterator != m_EchoServerClients.end())
        {
            m_EchoServerClients.erase(currClientSocketIterator);
            FD_CLR(clientSocketHandle, &m_ServerSocketHandleSet);
        }

        close(clientSocketHandle);

        if (strlen(message) > 0)
        {
            if (errorCode > 0)
            {
                perror(message);
            }
            else
            {
                std::fprintf(stdout, "Status: %s\n", message);
            }
        }
        
        return true;
    }

    bool CloseServer(const int& errorCode = 0, const char* message = "")
    {
        close(m_ServerSocketHandle);
        
        if (errorCode != 0)
        {
            perror(message);
        }
        
        return errorCode != 0;
    }

private:
    int m_ServerSocketListen = 0;
    int m_ServerSocketBound = 0;
    int m_ServerSocketHandle = 0;
    int m_ServerSocketHandleSetSize = 0;
    int m_ServerSocketReuseAddr = 0;
    int m_ServerSocketRecieved = 0;
    int m_ServerSocketLength = 0;

    unsigned int m_EchoServerBuffSize = 0;
    unsigned int m_EchoServerMaxPending = 0;

    char* m_ServerBuffer = nullptr;

    fd_set m_ServerSocketHandleSet;
    sockaddr_in m_ServerSocketAddress;
    std::map<int, sockaddr_in> m_EchoServerClients;
};

// Scaffold placeholder — Task 1.1 (TCP echo listener) replaces this.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "USAGE: ./server <port>\n");
        exit(1);
    }

    const uint16_t& bufferSize = 512;
    const uint16_t& serverPort = htons(atoi(argv[1]));
    TCPServer myServer(serverPort, bufferSize);
    
    return myServer.Update();
}
