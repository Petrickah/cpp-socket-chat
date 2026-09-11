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

class TCPConnection {
public:
    TCPConnection(const TCPConnection& other)
        : m_SocketBound(other.m_SocketBound)
        , m_SocketAvailable(other.m_SocketAvailable)
        , m_SocketConnected(other.m_SocketConnected)
        , m_SocketHandle(other.m_SocketHandle)
        , m_SocketBufferSize(other.m_SocketBufferSize)
        , m_SocketLength(other.m_SocketLength)
        , m_SocketAddress(other.m_SocketAddress)
        , m_SocketMaxPending(other.m_SocketMaxPending)
    {
        m_SocketBuffer = (char*)malloc(other.m_SocketBufferSize);
        memcpy(m_SocketBuffer, other.m_SocketBuffer, other.m_SocketBufferSize);
    }

    TCPConnection(const uint16_t serverPort = 6667, const int bufferSize = 512, const int maxPending = 5) : m_SocketAvailable(true)
    {
        m_SocketHandle = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP); // Create the new server socket using the TCP Protocol
        if (m_SocketHandle < 0 && this->Close(1, "Failed to create the server socket"))
        {
            m_SocketAvailable = false;
        }

        const int enable = 1;
        if (setsockopt(m_SocketHandle, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0 && this->Close(1, "Failed to set reuse address for the server socket"))
        {
            m_SocketAvailable = false;
        }

        if (m_SocketAvailable)
        {
            m_SocketBufferSize = bufferSize * sizeof(char);
            m_SocketBuffer = (char*)malloc(m_SocketBufferSize);
            m_SocketLength = sizeof(m_SocketAddress);
        
            // Construct the server SockAddr_In structure
            memset(&m_SocketAddress, 0, m_SocketLength);
            m_SocketAddress.sin_family = AF_INET;
            m_SocketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
            m_SocketAddress.sin_port = serverPort;

            m_SocketMaxPending.tv_usec = 0;
            m_SocketMaxPending.tv_sec  = maxPending;
        }
    }

    void Register(fd_set& serverSocketHandleSet, int& serverSocketHandleSetSize)
    {
        FD_ZERO(&serverSocketHandleSet);
        FD_SET(m_SocketHandle, &serverSocketHandleSet);

        serverSocketHandleSetSize = m_SocketHandle + 1;
    }

    void Bind()
    {
        m_SocketBound = true;
        m_SocketConnected = true;
        
        // Bind on the server Socket;
        if (bind(m_SocketHandle, (struct sockaddr *) &m_SocketAddress, m_SocketLength) < 0 && this->Close(1, "Failed to bind the server socket"))
        {
            m_SocketBound = false;
        }

        if (listen(m_SocketHandle, m_SocketMaxPending.tv_sec) < 0 && this->Close(1, "Failed to listen on server socket"))
        {
            m_SocketConnected = false;
        }
    }

    bool Connect(fd_set& serverSocketHandleSet)
    {
        m_SocketHandle = accept(m_SocketHandle, (struct sockaddr*)&m_SocketAddress, &m_SocketLength);

        if (m_SocketHandle < 0 && this->Close(1, "Failed to accept a connection"))
        {
            return false;
        }

        std::fprintf(stdout, "Client connected: %s:%d\n", inet_ntoa(m_SocketAddress.sin_addr), htons(m_SocketAddress.sin_port));
        FD_SET(m_SocketHandle, &serverSocketHandleSet);

        return true;
    }

    bool Handle()
    {
        ssize_t retVal = recvfrom(m_SocketHandle, m_SocketBuffer, m_SocketBufferSize, 0, (struct sockaddr*) &m_SocketAddress, &m_SocketLength);
        
        if (retVal < 1)
        {
            // We close the client with an warning if the client was closed
            return this->Close(0, "The client has been disconected");
        }
        else
        {
            m_SocketBuffer[retVal] = '\0';
            std::fprintf(stdout, "Recieved: %s\n", m_SocketBuffer);
    
            if (sendto(m_SocketHandle, m_SocketBuffer, retVal, 0, (struct sockaddr*) &m_SocketAddress, m_SocketLength) != retVal)
            {
                // We close the client with an error if the message couldn't be send
                return this->Close(1, "Failed to send bytes back to client");
            }
    
            if (std::strncmp(m_SocketBuffer, "EXIT", retVal) == 0)
            {
                // We close the client with an warning if the message recieved was EXIT
                return this->Close(0, "The client has been disconected");
            }
        }

        return true;
    }

    bool Close(const int& errorCode = 0, const char* message = "")
    {
        free(m_SocketBuffer);
        close(m_SocketHandle);

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
        
        return false;
    }

    bool operator==(const TCPConnection& other) const
    {
        return m_SocketHandle == other.GetSocketHandle();
    }

    constexpr TCPConnection& operator=(const TCPConnection& other)
    {
        m_SocketBound = other.m_SocketBound;
        m_SocketAvailable = other.m_SocketAvailable;
        m_SocketConnected = other.m_SocketConnected;
        m_SocketHandle = other.m_SocketHandle;
        m_SocketBufferSize = other.m_SocketBufferSize;
        m_SocketLength = other.m_SocketLength;
        m_SocketAddress = other.m_SocketAddress;
        m_SocketMaxPending = other.m_SocketMaxPending;

        memcpy(m_SocketBuffer, other.m_SocketBuffer, other.m_SocketBufferSize);

        return *this;
    }

public:
    struct timeval& GetTimeValue()
    {
        return m_SocketMaxPending;
    }

    int GetSocketHandle() const
    {
        return m_SocketHandle;
    }

    bool IsBound() const
    {
        return m_SocketBound && m_SocketConnected;
    }

    bool IsAvailable() const
    {
        return m_SocketAvailable;
    }

    bool IsReady(fd_set& serverSocketHandleSet) const
    {
        return FD_ISSET(m_SocketHandle, &serverSocketHandleSet);
    }

private:
    bool m_SocketBound = false;
    bool m_SocketAvailable = false;
    bool m_SocketConnected = false;

    int m_SocketHandle = 0;
    
    size_t m_SocketBufferSize = 0;
    socklen_t m_SocketLength;
    sockaddr_in m_SocketAddress;
    timeval m_SocketMaxPending;

    char* m_SocketBuffer = nullptr;
};

class TCPServer {
public:
    TCPServer(const uint16_t serverPort = 6667, const int bufferSize = 512, const int maxPending = 5) 
        : m_EchoServerConnection(serverPort, bufferSize, maxPending)
    {
        m_EchoServerConnection.Bind();
        m_EchoServerConnection.Register(m_ServerSocketHandleSet, m_ServerSocketHandleSetSize);
    }

    int Update()
    {
        while (m_EchoServerConnection.IsBound())
        {
            TCPConnection* clientConnection    = nullptr;
            
            if (SelectClient(m_ServerSocketHandleSetSize, m_ServerSocketHandleSet, clientConnection))
            {
                if (clientConnection == nullptr)
                {
                    TCPConnection clientConnectionNew = TCPConnection(m_EchoServerConnection);

                    if (clientConnectionNew.Connect(m_ServerSocketHandleSet) && m_EchoServerClients.find(clientConnectionNew.GetSocketHandle()) == m_EchoServerClients.end())
                    {
                        m_EchoServerClients.insert(std::make_pair(clientConnectionNew.GetSocketHandle(), clientConnectionNew));
    
                        if (clientConnectionNew.GetSocketHandle() + 1 > m_ServerSocketHandleSetSize)
                        {
                            m_ServerSocketHandleSetSize = clientConnectionNew.GetSocketHandle() + 1;
                        }
                    }
    
                    clientConnection = &clientConnectionNew;
                }
                else if (!clientConnection->Handle())
                {
                    std::map<int, TCPConnection>::const_iterator currClientSocketIterator = m_EchoServerClients.find(clientConnection->GetSocketHandle());
                    if (currClientSocketIterator != m_EchoServerClients.end())
                    {
                        m_EchoServerClients.erase(currClientSocketIterator);
                        FD_CLR(clientConnection->GetSocketHandle(), &m_ServerSocketHandleSet);
                    }
                }
            }
        }

        return m_EchoServerConnection.Close();
    }

private:
    bool UpdateSocketHandleSet(fd_set& rfds, TCPConnection*& clientConnection)
    {
        if (m_EchoServerConnection.IsReady(rfds))
        {
            clientConnection = nullptr;
            return true;
        }

        std::map<int, TCPConnection>::const_iterator currClientSocketIterator = m_EchoServerClients.begin();
        while (currClientSocketIterator != m_EchoServerClients.end())
        {
            clientConnection = const_cast<TCPConnection*>(&currClientSocketIterator->second);

            if (clientConnection->IsReady(rfds))
            {
                return true;
            }
            
            currClientSocketIterator++;
        }

        return false;
    }

    bool SelectClient(int nfds, fd_set rfds, TCPConnection*& clientConnection)
    {
        struct timeval tv = m_EchoServerConnection.GetTimeValue();
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
                if (UpdateSocketHandleSet(rfds, clientConnection) && clientConnection != nullptr)
                {
                    std::map<int, TCPConnection>::const_iterator currClientSocketIterator = m_EchoServerClients.find(clientConnection->GetSocketHandle());
                    if (currClientSocketIterator != m_EchoServerClients.end())
                    {
                        clientConnection = const_cast<TCPConnection*>(&currClientSocketIterator->second);
                    }
                }

                return true;
            }
        }
        
        return false;
    }

private:
    int m_ServerSocketHandleSetSize = 0;

    fd_set m_ServerSocketHandleSet;
    TCPConnection m_EchoServerConnection;
    std::map<int, TCPConnection> m_EchoServerClients;
};

// Scaffold placeholder — Task 1.1 (TCP echo listener) replaces this.
int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "USAGE: ./server <port>\n");
        exit(1);
    }

    const uint16_t& bufferSize = 512;
    const uint16_t& serverPort = htons(atoi(argv[1]));
    TCPServer myServer(serverPort, bufferSize);
    
    return myServer.Update();
}
