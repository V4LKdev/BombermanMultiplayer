
#ifndef BOMBERMAN_NET_NETCLIENT_H
#define BOMBERMAN_NET_NETCLIENT_H

#include <cstdint>
#include <memory>
#include <string_view>
#include <string>


namespace bomberman::net
{

    /**
     *  @brief NetClient - A client class for connecting to a Bomberman game server and handling the sending and receiving of network messages according to the Bomberman network protocol.
     */
    class NetClient {

    public:
        NetClient();
        ~NetClient();

        // Non-copyable (owns ENet resources)
        NetClient(const NetClient&) = delete;
        NetClient& operator=(const NetClient&) = delete;

        // Movable
        NetClient(NetClient&&) noexcept;
        NetClient& operator=(NetClient&&) noexcept;

        /**
         *  @brief Performs the handshake process with the server by connecting to the specified host and port, sending a Hello message with the player's name, and waiting for a Welcome response.
         *
         *  @param host The hostname or IP address of the server to connect to.
         *  @param port The port number on which the server is listening for connections.
         *  @param playerName The name of the player to send in the Hello message during the handshake. This name will be truncated if it exceeds the maximum allowed length defined in the protocol.
         *
         *  @return true if the handshake exchange succeeded, false if there was an error during the connection or handshake process.
         */
        [[deprecated("Use connect() instead")]]
        bool handshake(const std::string& host, uint16_t port, std::string_view playerName);

        /**
         *  @brief Connects to the server and performs the handshake process.
         *
         *  @param host The hostname or IP address of the server to connect to.
         *  @param port The port number on which the server is listening for connections.
         *  @param playerName The name of the player to send in the Hello message. This name will be truncated if it exceeds the maximum allowed length defined in the protocol.
         *
         *  @return true if the connection and handshake succeeded, false if there was an error during the connection or handshake process.
         */
        // TODO: replace bool with enum result
        bool connect(const std::string& host, uint16_t port, std::string_view playerName);

        /**
         *  @brief Disconnects from the server and cleans up any associated resources.
         */
        void disconnect();

        /**
         *  @brief Pumps the ENet host to process incoming and outgoing network events. Is non-blocking by default.
         *
         *  @param timeoutMs The maximum time in milliseconds to wait for network events before returning. A value of 0 means to return immediately if there are no events (non-blocking).
         */
        void pump(uint16_t timeoutMs = 0);

        /**
         *  @brief Checks if the client is currently connected to the server.
         *
         *  @return true if the client is connected, false otherwise.
         */
        [[nodiscard]]
        bool isConnected() const { return connected_; }

        /** @brief Returns the client ID assigned by the server during the handshake. Only valid after a successful connect(). */
        [[nodiscard]]
        uint32_t clientId() const { return clientId_; }

        /** @brief Returns the server tick rate received during the handshake. Only valid after a successful connect(). */
        [[nodiscard]]
        uint16_t serverTickRate() const { return serverTickRate_; }

    private:
        /**
         *  @brief Opaque ENet implementation detail.
         *
         *  Keeps <enet/enet.h> out of this header
         *  All ENet interaction happens through the Impl pointer in NetClient.cpp.
         */
        struct Impl;
        std::unique_ptr<Impl> impl_;

        bool initialized_ = false;
        bool connected_ = false;

        uint32_t clientId_ = 0;         ///< Assigned by server during handshake
        uint16_t serverTickRate_ = 0;   ///< Received from server during handshake

        bool initializeENet();
        void shutdownENet();
        bool performHandshake(std::string_view playerName);
    };

} // namespace bomberman::net

#endif //BOMBERMAN_NET_NETCLIENT_H
