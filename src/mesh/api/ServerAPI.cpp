#include "ServerAPI.h"
#include "configuration.h"
#include <Arduino.h>

template <typename T>
ServerAPI<T>::ServerAPI(T &_client) : StreamAPI(&client), concurrency::OSThread("ServerAPI"), client(_client)
{
    LOG_INFO("Incoming API connection");
}

template <typename T> ServerAPI<T>::~ServerAPI()
{
    client.stop();
}

template <typename T> void ServerAPI<T>::close()
{
    client.stop(); // drop tcp connection
    StreamAPI::close();
}

/// Check the current underlying physical link to see if the client is currently connected
template <typename T> bool ServerAPI<T>::checkIsConnected()
{
    return client.connected();
}

template <class T> int32_t ServerAPI<T>::runOnce()
{
    if (client.connected()) {
        return StreamAPI::runOncePart();
    } else {
        LOG_INFO("Client dropped connection, suspend API service");
        enabled = false; // we no longer need to run
        return 0;
    }
}

template <class T, class U> APIServerPort<T, U>::APIServerPort(int port) : U(port), concurrency::OSThread("ApiServer") {}

template <class T, class U> void APIServerPort<T, U>::init()
{
    U::begin();
}

template <class T, class U> int32_t APIServerPort<T, U>::runOnce()
{
    // Delete any dropped connections and compact so nulls stay at the end.
    for (size_t i = 0; i < clients.size();) {
        if (clients[i] && !clients[i]->isAlive()) {
            LOG_INFO("TCP connection %u dropped", (unsigned)i);
            delete clients[i];

            // Shift items left to fill the gap
            for (size_t j = i; j < clients.size() - 1; j++) {
                clients[j] = clients[j + 1];
            }
            clients[clients.size() - 1] = nullptr;

            // Do not increment i; re-check the new occupant at index i
        } else {
            i++;
        }
    }

    // See if there's a new connection to accept
#ifdef ARCH_ESP32
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    auto client = U::accept();
#else
    auto client = U::available();
#endif
#elif defined(ARCH_RP2040)
    auto client = U::accept();
#else
    auto client = U::available();
#endif

    if (client) {
        // If we are full: drop the oldest connection (last entry in clients array)
        if (clients.back()) {
#if RAK_4631
            // RAK13800 Ethernet requests periodically take more time
            // This backoff addresses most cases keeping max wait < 1s
            // Reconnections are delayed by full wait time
            if (waitTime < 400) {
                waitTime *= 2;
                LOG_INFO("Previous TCP connection still open, try again in %dms", waitTime);
                return waitTime;
            }
#endif

            LOG_INFO("Force closing oldest TCP connection to accept new one");
            delete clients.back();
            clients.back() = nullptr;
        }

        // Shift down so we can insert at index 0
        for (size_t i = clients.size() - 1; i > 0; i--) {
            clients[i] = clients[i - 1];
        }

        // Instantiate new connection and insert at index 0. (Always inserts at index 0 to maintain sort by connection age.)
        LOG_INFO("Accepting new incoming TCP connection");
        clients[0] = new T(client);
    }

#if RAK_4631
    waitTime = 100;
#endif
    return 100; // only check occasionally for incoming connections
}
