
#ifndef BOMBERMAN_NET_NETCLIENT_H
#define BOMBERMAN_NET_NETCLIENT_H

#include <cstdint>
#include <string_view>
#include <string>

namespace bomberman::net
{

    /**
     *  @brief NetClient - A client class for connecting to a Bomberman game server and handling the sending and receiving of network messages according to the Bomberman network protocol.
     */
    class NetClient {

    public:
        /**
         *  @brief Performs the handshake process with the server by connecting to the specified host and port, sending a Hello message with the player's name, and waiting for a Welcome response.
         *
         *  @param host The hostname or IP address of the server to connect to.
         *  @param port The port number on which the server is listening for connections.
         *  @param playerName The name of the player to send in the Hello message during the handshake. This name will be truncated if it exceeds the maximum allowed length defined in the protocol.
         *
         *  @return true if the handshake exchange succeeded, false if there was an error during the connection or handshake process.
         */
        bool handshake(const std::string& host, uint16_t port, std::string_view playerName);
    };

} // namespace bomberman::net

#endif //BOMBERMAN_NET_NETCLIENT_H
