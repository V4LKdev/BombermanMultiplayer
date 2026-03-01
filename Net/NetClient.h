#ifndef BOMBERMAN_NET_NETCLIENT_H
#define BOMBERMAN_NET_NETCLIENT_H

#include <memory>
#include <string_view>
#include <string>

#include "NetCommon.h"


namespace bomberman::net
{

    /**
     *  @brief NetClient - A client class for connecting to a Bomberman game server and handling the sending and receiving of network messages according to the Bomberman network protocol.
     */
    class NetClient {

    public:
        NetClient();
        ~NetClient();

        // Non-copyable
        NetClient(const NetClient&) = delete;
        NetClient& operator=(const NetClient&) = delete;

        // Movable
        NetClient(NetClient&&) noexcept;
        NetClient& operator=(NetClient&&) noexcept;

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
         *  @brief Sends an input message to the server with the given input state and client tick.
         *
         *  @param input The MsgInput struct containing the player's input state to send to the server.
         *  @param clientTick The client's current tick value to include in the packet header for sync purposes.
         */
        void sendInput(const MsgInput& input, uint32_t clientTick);



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

        /** @brief Processes a validated Welcome payload. Called by the dispatcher handler. */
        void handleWelcome(const uint8_t* payload, std::size_t payloadSize);

        /** @brief Cleans up state after the server disconnects us. Does not send a disconnect request. */
        void handleRemoteDisconnect();
    };

} // namespace bomberman::net

#endif //BOMBERMAN_NET_NETCLIENT_H
