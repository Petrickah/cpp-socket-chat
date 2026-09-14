#include <iostream>
#include <map>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

class TCPConnection {
public:
    TCPConnection() = default;

    TCPConnection(const TCPConnection& other)
    {
        this->Copy(other);
    }

    TCPConnection(fd_set& rfds, std::map<int, TCPConnection>& echoServerClients, const uint16_t serverPort = 6667, const int socketBufferSize = 512, const int maxPending = 5) : m_SocketAvailable(true)
    {
        m_SocketHandle = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP); // Create the new server socket using the TCP Protocol
        if (m_SocketHandle < 0 && this->Close(rfds, echoServerClients, 1, "Failed to create the socket"))
        {
            m_SocketAvailable = false;
        }

        const int enable = 1;
        if (setsockopt(m_SocketHandle, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0 && this->Close(rfds, echoServerClients, 1, "Failed to set reuse address for the server socket"))
        {
            m_SocketAvailable = false;
        }

        if (m_SocketAvailable)
        {
            // Construct the socket buffer
            m_SocketBuffer = "";
            m_SocketBuffer.reserve(socketBufferSize);
            m_SocketBufferSize = m_SocketBuffer.capacity();

            m_SocketLength = sizeof(m_SocketAddress);
            
            // Construct the socket SockAddr_In structure
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

    void Bind(fd_set& rfds, std::map<int, TCPConnection>& echoServerClients)
    {
        m_SocketBound = true;
        m_SocketConnected = true;
        
        // Bind on the server Socket;
        if (bind(m_SocketHandle, (struct sockaddr *) &m_SocketAddress, m_SocketLength) < 0 && this->Close(rfds, echoServerClients, 1, "Failed to bind the server socket"))
        {
            m_SocketBound = false;
        }

        if (listen(m_SocketHandle, m_SocketMaxPending.tv_sec) < 0 && this->Close(rfds, echoServerClients, 1, "Failed to listen on server socket"))
        {
            m_SocketConnected = false;
        }
    }

    bool Close(fd_set& rfds, std::map<int, TCPConnection>& echoServerClients, const int& errorCode = 0, const char* message = "")
    {
        auto currClientSocketIterator = echoServerClients.find(m_SocketHandle);

        if (currClientSocketIterator != echoServerClients.end())
        {
            echoServerClients.erase(currClientSocketIterator);

            switch (errorCode)
            {
                case (0):
                {
                    if (strlen(message) > 0)
                    {
                        std::fprintf(stdout, "Status: %s\n", message);
                    }

                    break;
                }
                default:
                {
                    if (strlen(message) > 0)
                    {
                        perror(message);
                    }

                    break;
                }
            }

            close(m_SocketHandle);
            FD_CLR(m_SocketHandle, &rfds);
        }

        return true;
    }


    TCPConnection& Connect(fd_set& rfds, std::map<int, TCPConnection>& echoServerClients)
    {
        m_SocketConnected = true;
        
        m_SocketHandle = accept(m_SocketHandle, (struct sockaddr*)&m_SocketAddress, &m_SocketLength);
        if (m_SocketHandle < 0 && this->Close(rfds, echoServerClients, 1, "Failed to accept a connection"))
        {
            m_SocketConnected = false;
        }

        if (m_SocketConnected)
        {
            std::fprintf(stdout, "Client connected: %s:%d\n", inet_ntoa(m_SocketAddress.sin_addr), htons(m_SocketAddress.sin_port));
            
            // Socket is connected
            FD_SET(m_SocketHandle, &rfds);
            
            // Construct the socket buffer
            m_SocketBuffer = "";
            m_SocketBuffer.reserve(m_SocketBufferSize);
        }

        return this->Copy(*this);
    }

    TCPConnection& Update(fd_set& rfds, fd_set crfds, std::map<int, TCPConnection>& echoServerClients)
    {
        for (auto clientConnection : echoServerClients)
        {
            TCPConnection& connection = clientConnection.second;
            if (connection.IsReady(crfds))
            {
                return this->Copy(connection);
            }
        }

        if (this->IsReady(crfds))
        {
            return this->Connect(rfds, echoServerClients);
        }

        return this->Copy(*this);
    }

    TCPConnection& Select(int nfds, fd_set& rfds, std::map<int, TCPConnection>& echoServerClients)
    {
        fd_set crfds = rfds;
        struct timeval tv = m_SocketMaxPending;
        int retVal = select(nfds, &crfds, NULL, NULL, &tv);

        switch (retVal)
        {
            case (-1):
            {
                // Error - We consider the server is free to choose from the next clients
                return this->Copy(*this);
            }
            case (0):
            {
                // Timeout - We consider the server is free to choose from the next clients
                return this->Copy(*this);
            }
            default:
            {
                // Accepted - We consider the server is ready to recieve something from a client
                return this->Update(rfds, crfds, echoServerClients);
            }
        }
    }

    bool Handle(fd_set& rfds, std::map<int, TCPConnection>& echoServerClients)
    {
        m_SocketBuffer = "";
        m_SocketBuffer.resize(m_SocketBufferSize);
        
        switch (ssize_t retVal = recv(m_SocketHandle, m_SocketBuffer.data(), m_SocketBuffer.size(), 0))
        {
            case (-1):
            {
                // ERROR - The client was previously disconnected?
                return false;
            }
            case (0):
            {
                // CLEAR - The client didn't sent anything yet?
                return this->Close(rfds, echoServerClients, 0, "The client has been disconected");
            }
            default:
            {
                std::fprintf(stdout, "%s\n", m_SocketBuffer.data());
        
                if (send(m_SocketHandle, m_SocketBuffer.data(), retVal, 0) != retVal)
                {
                    return true;
                }
            }
        }

        return true;
    }

    bool operator==(const TCPConnection& other) const
    {
        return m_SocketHandle == other.GetSocketHandle();
    }

    TCPConnection& Copy(const TCPConnection& other)
    {
        m_SocketBound = other.m_SocketBound;
        m_SocketAvailable = other.m_SocketAvailable;
        m_SocketConnected = other.m_SocketConnected;
        m_SocketHandle = other.m_SocketHandle;
        m_SocketLength = other.m_SocketLength;
        m_SocketAddress = other.m_SocketAddress;
        m_SocketMaxPending = other.m_SocketMaxPending;
        
        m_SocketBufferSize = other.m_SocketBufferSize;
        m_SocketBuffer.resize(other.m_SocketBufferSize);
        other.m_SocketBuffer.copy((char*)m_SocketBuffer.data(), other.m_SocketBufferSize, 0);

        return *this;
    }

    TCPConnection& operator=(const TCPConnection& other)
    {
        return this->Copy(other);
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

    bool IsConnected() const
    {
        return m_SocketConnected;
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
    
    std::string m_SocketBuffer;
    ssize_t m_SocketBufferSize = 0;
    socklen_t m_SocketLength;
    sockaddr_in m_SocketAddress;
    timeval m_SocketMaxPending;

};

class TCPServer {
public:
    TCPServer(const uint16_t serverPort = 6667, const int bufferSize = 512, const int maxPending = 5) 
        : m_EchoServerConnection(m_ServerSocketHandleSet, m_EchoServerClients, serverPort, bufferSize, maxPending)
    {
        m_EchoServerConnection.Bind(m_ServerSocketHandleSet, m_EchoServerClients);
        m_EchoServerConnection.Register(m_ServerSocketHandleSet, m_ServerSocketHandleSetSize);
    }

    bool Update()
    {
        while (m_EchoServerConnection.IsBound())
        {
            TCPConnection clientConnection = TCPConnection(m_EchoServerConnection)
                .Select(m_ServerSocketHandleSetSize, m_ServerSocketHandleSet, m_EchoServerClients);
            
            if (clientConnection == m_EchoServerConnection)
            {
                continue;
            }

            if (m_EchoServerClients.find(clientConnection.GetSocketHandle()) == m_EchoServerClients.end())
            {
                m_EchoServerClients.insert(std::make_pair(clientConnection.GetSocketHandle(), clientConnection));

                if (clientConnection.GetSocketHandle() + 1 > m_ServerSocketHandleSetSize)
                {
                    m_ServerSocketHandleSetSize = clientConnection.GetSocketHandle() + 1;
                }
            }
            else
            {
                clientConnection.Handle(m_ServerSocketHandleSet, m_EchoServerClients);
            }
        }

        return true;
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
    if (argc != 2)
    {
        std::fprintf(stderr, "USAGE: ./server <port>\n");
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);

    const uint16_t& bufferSize = 512;
    const uint16_t& serverPort = htons(atoi(argv[1]));
    TCPServer myServer(serverPort, bufferSize);
    
    return myServer.Update();
}
