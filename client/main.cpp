#include <iostream>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

class TCPClient {
public:
    enum class Status
    {
        UNKNOWN,
        READY_TO_SEND,
        READY_TO_RECIEVE,
        RECIEVE,
        SEND,
    };

    TCPClient(const int bufferSize): m_ClientBuffSize(bufferSize)
    {
        // Create the new client socket using the TCP Protocol
        m_ClientSocketHandle = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (m_ClientSocketHandle < 0)
        {
            // We close the client with an error if the socket creation fails
            exit(this->Close(1, "Failed to create the socket"));
        }

        m_ClientStatus = TCPClient::Status::UNKNOWN;
    }

    TCPClient::Status GetClientStatus() const
    {
        return m_ClientStatus;
    }

    void SetClientStatus(const TCPClient::Status& value)
    {
        m_ClientStatus = value;
    }

    bool Connect(const in_addr_t& serverAddress, const uint16_t& serverPort)
    {
        memset(&m_ClientEchoServer, 0, sizeof(m_ClientEchoServer));
        m_ClientEchoServer.sin_family = AF_INET;
        m_ClientEchoServer.sin_addr.s_addr = serverAddress;
        m_ClientEchoServer.sin_port = serverPort;

        // Establish a connection
        m_ClientSocketConnection = connect(m_ClientSocketHandle, (struct sockaddr*) &m_ClientEchoServer, sizeof(m_ClientEchoServer));

        if (m_ClientSocketConnection < 0)
        {
            // We close the client with an error if connection couldn't be established
            return this->Close(1, "Failed to establish a connection with the server");
        }

        m_ClientStatus = TCPClient::Status::READY_TO_SEND;

        return true;
    }

    bool Send(const std::string& inputMessage)
    {
        m_ClientBuffer += inputMessage;
        size_t retVal = send(m_ClientSocketHandle, inputMessage.c_str(), inputMessage.length(), 0);

        if (retVal != inputMessage.length())
        {
            // We close the client with an error if the message sent was broken.
            return this->Close(1, "Mismatch in number of sent bytes");
        }

        return true;
    }

    bool Recieve()
    {
        m_ClientBuffer = "";
        m_ClientBuffer.resize(m_ClientBuffSize);

        switch (recv(m_ClientSocketHandle, m_ClientBuffer.data(), m_ClientBuffer.size(), 0))
        {
            case (-1):
            {
                // ERROR - The server was previously disconnected?
                return false;
            }
            case (0):
            {
                // CLEAR - The client didn't sent anything yet?
                return this->Close(1, "The client didn't sent anything yet");
            }
            default:
            {
                std::fprintf(stdout, "%s", m_ClientBuffer.data());
            }
        }

        return true;
    }

    bool Close(const int& errorCode = 0, const char* message = "")
    {
        close(m_ClientSocketHandle);

        if (errorCode)
        {
            perror(message);
        }

        return false;
    }

private:
    int m_ClientSocketHandle = 0;
    int m_ClientSocketConnection = 0;
    
    unsigned int m_ClientBuffSize = 0;

    std::string m_ClientBuffer = "";
    struct sockaddr_in m_ClientEchoServer;

    TCPClient::Status m_ClientStatus;
};

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "USAGE: ./client <server_ip> <port>\n");
        exit(1);
    }
    
    const uint16_t& bufferSize = 512;

    TCPClient myClient(bufferSize);
    
    const in_addr_t& serverAddress = inet_addr(argv[1]);
    const uint16_t& serverPort = htons(atoi(argv[2]));

    if (serverAddress == INADDR_NONE)
    {
        return myClient.Close(1, "IP Address was incorrectly typed");
    }

    if (myClient.Connect(serverAddress, serverPort))
    {
        std::string inputMessage = "";

        // Send a message to the server to recieve it back
        while (inputMessage.compare(0, inputMessage.length(), "EXIT\r\n"))
        {
            switch (myClient.GetClientStatus())
            {
                case TCPClient::Status::READY_TO_RECIEVE:
                {
                    std::fprintf(stdout, "(Type EXIT to close this client) <<< \n");

                    // Recieve the information from server
                    myClient.SetClientStatus(TCPClient::Status::RECIEVE);

                    break;
                }
                case TCPClient::Status::READY_TO_SEND:
                {
                    std::fprintf(stdout, "(Type EXIT to close this client) >>> \n");
                    
                    // Send the information to server
                    myClient.SetClientStatus(TCPClient::Status::SEND);
                    
                    break;
                }
                case TCPClient::Status::RECIEVE:
                {
                    if (myClient.Recieve())
                    {
                        myClient.SetClientStatus(TCPClient::Status::READY_TO_SEND);
                    }
                    
                    break;
                }
                case TCPClient::Status::SEND:
                {
                    std::getline(std::cin, inputMessage);
                    myClient.Send(inputMessage+="\r\n");

                    if (inputMessage == "\r\n")
                    {
                        myClient.SetClientStatus(TCPClient::Status::READY_TO_RECIEVE);
                    }
                    
                    break;
                }
                case TCPClient::Status::UNKNOWN:
                {
                    break;
                }
            }
        }
    }

    return 0;
}
