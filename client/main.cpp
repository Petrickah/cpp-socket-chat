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
    TCPClient(const int bufferSize): m_ClientBuffSize(bufferSize) {
        // Create the new client socket using the TCP Protocol
        m_ClientSocketHandle = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (m_ClientSocketHandle < 0) {
            // We close the client with an error if the socket creation fails
            exit(this->Close(1, "Failed to create the socket"));
        }
    }

    ~TCPClient() {
        delete[] m_ClientBuffer;
    }

    int Connect(const in_addr_t& serverAddress, const uint16_t& serverPort) {
        memset(&m_ClientEchoServer, 0, sizeof(m_ClientEchoServer));
        m_ClientEchoServer.sin_family = AF_INET;
        m_ClientEchoServer.sin_addr.s_addr = serverAddress;
        m_ClientEchoServer.sin_port = serverPort;

        // Establish a connection
        m_ClientSocketConnection = connect(m_ClientSocketHandle, (struct sockaddr*) &m_ClientEchoServer, sizeof(m_ClientEchoServer));

        if (m_ClientSocketConnection < 0) {
            // We close the client with an error if connection couldn't be established
            return this->Close(1, "Failed to establish a connection with the server");
        }

        // When we establish a connection, only then we can allocate a buffer
        m_ClientBuffer = new char[m_ClientBuffSize];

        return 0;
    }

    int Send(const std::string& inputMessage) {
        m_ClientRecieved = 0;
        m_ClientEchoLength = inputMessage.length();

        if (send(m_ClientSocketHandle, inputMessage.c_str(), m_ClientEchoLength, 0) != m_ClientEchoLength) {
            // We close the client with an error if the message sent was broken.
            return this->Close(1, "Mismatch in number of sent bytes");
        }

        if (m_ClientEchoLength > 0) {
            // Recieve the word back from the server
            while (m_ClientRecieved < m_ClientEchoLength) {
                int bytes = recv(m_ClientSocketHandle, m_ClientBuffer, m_ClientBuffSize-1, 0);
                if (bytes < 1) {
                    // We close the client with an error if the message wasn't recieved from server
                    return this->Close(1, "Failed to receive bytes from the server");
                }
                
                m_ClientRecieved += bytes;
                m_ClientBuffer[bytes] = '\0';
                
                std::fprintf(stdout, "Recieved: %s\n", m_ClientBuffer);

                if (std::strncmp(m_ClientBuffer, "EXIT", m_ClientRecieved) == 0) {
                    return 1;
                }
            }
        }

        return 0;
    }

    int Close(const int& errorCode = 0, const char* message = "") {
        close(m_ClientSocketHandle);

        if (errorCode) {
            perror(message);
        }

        return errorCode;
    }

private:
    int m_ClientSocketHandle = 0;
    int m_ClientSocketConnection = 0;
    
    unsigned int m_ClientRecieved = 0;
    unsigned int m_ClientEchoLength = 0;
    unsigned int m_ClientBuffSize = 0;

    char* m_ClientBuffer = nullptr;
    struct sockaddr_in m_ClientEchoServer;

};

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "USAGE: ./client <server_ip> <port>\n");
        exit(1);
    }
    
    const uint16_t& bufferSize = 512;

    TCPClient myClient(bufferSize);
    
    const in_addr_t& serverAddress = inet_addr(argv[1]);
    const uint16_t& serverPort = htons(atoi(argv[2]));

    if (serverAddress == INADDR_NONE) {
        return myClient.Close(1, "IP Address was incorrectly typed");
    }

    if (myClient.Connect(serverAddress, serverPort) == 0) {
        std::string inputMessage = "";
    
        // Send a message to the server to recieve it back
        while (myClient.Send(inputMessage) == 0) {
            // Read the message we want to send until we type EXIT
            std::cout << "(Type EXIT to close this client) >>> ";
            std::getline(std::cin, inputMessage);
        }

        return myClient.Close();
    }

    return 1;
}
