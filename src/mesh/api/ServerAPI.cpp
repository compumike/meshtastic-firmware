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

template <class T, class U> int APIServerPort<T, U>::countActiveClients() const
{
    int count = 0;
    for (auto *c : clients)
        if (c)
            ++count;
    return count;
}

template <class T, class U> int32_t APIServerPort<T, U>::runOnce()
{
    // Finish any deletes that were deferred from a prior pass of runOnce.
    // (These threads are safe to delete now since their thread was disabled in the prior pass.)
    for (auto &p : clientsPendingDelete) {
        if (p) {
            delete p;
            p = nullptr;
        }
    }

    // Delete any dropped connections and compact so nulls stay at the end.
    for (size_t i = 0; i < clients.size();) {
        if (clients[i] && !clients[i]->isAlive()) {
            LOG_INFO("TCP connection %u dropped (%d active)", (unsigned)i, countActiveClients() - 1);

            // Disable thread and remove from clients array
            T *toDelete = clients[i];
            toDelete->disableThread();

            // Add it to clientsPendingDelete so we can delete on the next round.
            // (Careful: it's not safe to delete it now since the thread may still be scheduled for later in this same loop!)
            clientsPendingDelete[i] = toDelete; // Safe to put in clientsPendingDelete[i] since clientsPendingDelete will be empty
                                                // at the start of this for loop

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
            // Disable oldest and mark for deletion on next pass
            T *oldest = clients.back();
            oldest->disableThread();
            clientsPendingDelete[0] = oldest; // Safe to put in clientsPendingDelete[0] since it must be empty if clients is full!
            clients.back() = nullptr;
            LOG_INFO("Force closing oldest TCP connection to accept new one (%d active)", countActiveClients());
        }

        // Shift down so we can insert at index 0
        for (size_t i = clients.size() - 1; i > 0; i--) {
            clients[i] = clients[i - 1];
        }

        // Instantiate new connection and insert at index 0. (Always inserts at index 0 to maintain sort by connection age.)
        clients[0] = new T(client);
        LOG_INFO("Accepting new incoming TCP connection (%d active)", countActiveClients());
    }

#if RAK_4631
    waitTime = 100;
#endif
    return 100; // only check occasionally for incoming connections
}
