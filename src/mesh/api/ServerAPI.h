#pragma once

#include "StreamAPI.h"
#include <array>

#define SERVER_API_DEFAULT_PORT 4403
#define SERVER_API_MAX_TCP_CLIENTS 4

/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
template <class T> class ServerAPI : public StreamAPI, private concurrency::OSThread
{
  private:
    T client;

  public:
    explicit ServerAPI(T &_client);

    virtual ~ServerAPI();

    /// override close to also shutdown the TCP link
    virtual void close();

    /// Public helper so APIServerPort can detect dropped connections
    bool isAlive() { return checkIsConnected(); }

  protected:
    /// We override this method to prevent publishing EVENT_SERIAL_CONNECTED/DISCONNECTED for wifi links (we want the board to
    /// stay in the POWERED state to prevent disabling wifi)
    virtual void onConnectionChanged(bool connected) override {}

    virtual int32_t runOnce() override; // Check for dropped client connections

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;
};

/**
 * Listens for incoming connections and does accepts and creates instances of ServerAPI as needed
 */
template <class T, class U> class APIServerPort : public U, private concurrency::OSThread
{
    // Array of currently open connections. Newest is always at index 0.
    std::array<T *, SERVER_API_MAX_TCP_CLIENTS> clients = {};

#if defined(RAK_4631) || defined(RAK11310)
    // Track wait time for RAK13800 Ethernet requests
    int32_t waitTime = 100;
#endif

  public:
    explicit APIServerPort(int port);

    int countActiveClients() const;

    // Destructor, for deInitApiServer
    ~APIServerPort()
    {
        for (auto &c : clients) {
            if (c) {
                delete c;
                c = nullptr;
            }
        }
    }

    void init();

  protected:
    int32_t runOnce() override;
};
