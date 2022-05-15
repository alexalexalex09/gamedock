/*  Auto Sync for ESP8266
    by Alex Becker
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include "macUtils.h"

/**
 * @brief PIN number of the sync button.
 *
 * TODO: Change this!
 * For a momentary switch hooked up to GPIO 4, change this to wherever you have a button hooked up
 */
static const int SYNC_BUTTON = 4;

/**
 * @brief Standard Wifi channel for all devices
 * TODO: Change this to your preferred wifi channel
 * Initially using channel 1 to see how it goes
 */
static const int WIFI_CHANNEL = 1;

/**
 * @brief the maximum number of peers we're willing to allow
 * TODO: Set your own maximum number of peers, considering how many are available on your hardware
 *
 */
static const int MAX_PEERS = 20;

/**
 * @brief variable to track sync status, 0=off
 *
 *
 */
int g_syncStarted = 0;

/**
 * @brief variable to track time for syncing
 *
 */
unsigned long g_startSyncTime = 0;

/**
 * @brief to show if the last broadcast was not sent
 *
 */
int g_lastDeliveryFailed = -1;

/**
 * @brief address used for broadcasting via espnow
 *
 */
uint8_t BROADCAST_ADDRESS[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/**
 * @brief variable for holding this device's mac address
 *
 */
uint8_t OWN_MAC_ADDRESS[6];

/**
 * @brief variable to hold a dummy address (all zeroes)
 *
 */
uint8_t DUMMY_ADDRESS[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
 * @brief variable for showing whether the peer list has been confirmed
 *
 */
int g_ownPeerListConfirmed = 0;

/**
 * @brief a global array of peers
 *
 *
 */
uint8_t g_peers[MAX_PEERS][6] = {0};

/**
 * @brief a global array of peers used to reorder turns
 *
 *
 */
uint8_t g_tempPeers[MAX_PEERS][6] = {0};

/**
 * @brief the current active player's mac address *
 *
 */
uint8_t g_currentPlayer[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
 * @brief a variable to hold the first player's address as soon as it's set
 *
 *
 */
uint8_t g_firstPlayer[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
 * @brief a structure to send data, which must be matched on the receiving side.
 *
 * uint8_t address[6]:
 * The address that is currently being sent.
 *
 * uint8_t peers[MAX_PEERS][6] = {0} :
 * A two dimensional array: 20 instances of 6 digit mac addresses.
 *
 * int purpose:
 * 1: I'm syncing and this is my MAC address
 * 2: This is the list of peers that I have
 *
 * int resend = 0 to indicate if this is being resent because of a reported sending failure.
 *
 */
typedef struct autosync_send_struct
{
    int purpose;
    uint8_t address[6];
    uint8_t peers[MAX_PEERS][6] = {0};
    int resend = 0;
} autosync_send_struct;

// Create a struct_message called sending to store variables to be sent
autosync_send_struct sending = {0};

// Create a struct_message called receiving
autosync_send_struct lastSentPacket = {0};

/********************************************************************************************************************************************
 *                           Functions
 ********************************************************************************************************************************************/

/**
 * @brief send a autosync_send_struct to all peers
 * Depends on printMacAddress
 *
 *
 */
autosync_send_struct sendPacket(autosync_send_struct toSend)
{
    // Send message via ESP-NOW
    Serial.print("Message sending: command: ");
    Serial.println(toSend.purpose);
    Serial.println("Sending Mac: ");
    printMacAddress(toSend.address);
    Serial.println();
    Serial.print("Send result: ");
    Serial.println(esp_now_send(0, (uint8_t *)&toSend, sizeof(toSend)));
    return toSend; // return this to be used again in case of failure
}

/********************************************************************************************************************************************
 *                           Send Failure Catchall
 ********************************************************************************************************************************************/
void checkFailure()
{
    if (g_lastDeliveryFailed == 1) // If a send failure was detected, stop execution for 50ms and resend
    {
        delay(50);
        g_lastDeliveryFailed = 0; // Reset variable to indicate the failure was noticed
        lastSentPacket.resend = 1;
        sendPacket(lastSentPacket);
    }
}

/**
 * @brief check an incoming address against the global list of peers to see if it's new, and if so, add it to the list.
 *  Depends on areMacAddressesEqual and pushNewMacAddress
 *
 *  @param incomingAddress a autosync_send_struct to check
 *
 *
 */
void checkAndSyncAddress(autosync_send_struct incomingAddress)
{
    int duplicatePeer = 0;                                             // Initialize a duplicate indicator
    if (!areMacAddressesEqual(DUMMY_ADDRESS, incomingAddress.address)) // If the incoming address isn't zeroes
    {
        for (int i = 0; i < MAX_PEERS; i++) // iterate over the array of peers
        {
            if (areMacAddressesEqual(g_peers[i], DUMMY_ADDRESS)) // if the end of the peers array has been reached, stop
            {
                break;
            }
            else
            {
                if (areMacAddressesEqual(g_peers[i], incomingAddress.address)) // check each el_peer to see if its address is the incoming address
                {
                    duplicatePeer = 1; // if it's duplicate, set duplicate to 1 (true)
                    break;             // no need to keep searching, we know it's a duplicate
                }
            }
        }
        if (duplicatePeer == 0) // after iterating, if no duplicate was detected
        {
            pushNewMacAddress(g_peers, incomingAddress.address); // push it to the array
        }
    }
    else
    {
        Serial.println("Found a dummy address while checking and syncing");
    }
}

/**
 * @brief Send this device's mac address to the broadcast peer
 * Depends on copyMacAddress and sendPacket
 *
 */
void sendMacAddress()
{
    autosync_send_struct sending = {0};               // Create a packet to send
    sending.purpose = 1;                              // set purpose to 1 = I'm syncing and this is my Mac address
    copyMacAddress(sending.address, OWN_MAC_ADDRESS); // Include the mac address of this device.
    lastSentPacket = sendPacket(sending);             // Send the packet
}

/**
 * @brief Remove the BROADCAST_ADDRESS from the esp now peer list and adds all peers in the peers global
 *
 *
 */
void switchFromBroadcastToPeers()
{
    esp_now_del_peer(BROADCAST_ADDRESS); // Remove the broadcast address
    for (int i = 0; i < MAX_PEERS; i++)
    {
        if (!areMacAddressesEqual(g_peers[i], DUMMY_ADDRESS) && !areMacAddressesEqual(g_peers[i], OWN_MAC_ADDRESS)) // if the current peer is not empty and not this device
        {
            esp_now_add_peer(g_peers[i], ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0); // add the current peer
        }
        else
        {
            break; // otherwise we're done
        }
    }
}

/**
 * @brief Remove the peers in the peers global from the esp now peer list and adds the BROADCAST_ADDRESS
 *
 *
 */
void switchFromPeersToBroadcast()
{
    for (int i = 0; i < MAX_PEERS; i++)
    {
        if (!areMacAddressesEqual(g_peers[i], DUMMY_ADDRESS)) // if the current peer is not empty
        {
            esp_now_del_peer(g_peers[i]); // delete it from the list
        }
        else
        {
            break; // otherwise we're done
        }
    }
    esp_now_add_peer(BROADCAST_ADDRESS, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0);
}

void copyPeers(uint8_t dest[MAX_PEERS][6], uint8_t source[MAX_PEERS][6])
{
    for (int i = 0; i < MAX_PEERS; i++)
    {
        for (int j = 0; j < 6; j++)
        {
            dest[i][j] = source[i][j];
        }
    }
}

/**
 * @brief Send the list of peers out to all currently registered peers
 *
 */
void confirmSync()
{
    Serial.println("Confirming sync...");
    delay(10);
    autosync_send_struct sending = {0};             // Create a packet to send
    copyPeers(sending.peers, g_peers);              // Attach the full list of peers
    copyMacAddress(sending.address, DUMMY_ADDRESS); // Send a dummy address
    sending.purpose = 2;                            // 2: this is the list of peers I have
    lastSentPacket = sendPacket(sending);           // Send the packet
}

/**
 * @brief Given a struct with a peers parameter, push it to the global peers vector if it's new
 *
 *
 */
void confirmPeerList(autosync_send_struct incomingPeers)
{
    int peerListChanged = 0;            // initialize an indicator for whether the peer list has changed
    int myMacIncluded = 0;              // initialize an indicator for whether the current device's mac address is in the peer list
    for (int i = 0; i < MAX_PEERS; i++) // Loop through the incoming peers
    {
        Serial.println("Checking MAC addresses:"); // Logging
        printMacAddress(incomingPeers.peers[i]);   // Logging
        Serial.println();                          // Logging
        if (!areMacAddressesEqual(incomingPeers.peers[i], DUMMY_ADDRESS))
        {
            if (!areMacAddressesEqual(incomingPeers.peers[i], BROADCAST_ADDRESS))
            {
                if (areMacAddressesEqual(incomingPeers.peers[i], OWN_MAC_ADDRESS))
                {
                    myMacIncluded = 1;
                }
                int duplicateFound = 0;
                for (int j = 0; j < MAX_PEERS; j++) // for each incoming peer, loop through the local list of peers
                {
                    if (!areMacAddressesEqual(g_peers[j], DUMMY_ADDRESS))
                    {
                        if (areMacAddressesEqual(g_peers[j], incomingPeers.peers[i]))
                        {
                            Serial.println("^ duplicate"); // logging
                            duplicateFound = 1;            // A duplicate was found
                            break;                         // if so, no need to further analyze, move on to the next address
                        }
                    }
                    else
                    {
                        break; // The end was found, stop iterating
                    }
                }
                if (duplicateFound == 0) // after iterating, if this is not a duplicate, push it to the global peers list
                {
                    pushNewMacAddress(g_peers, incomingPeers.peers[i]);
                    Serial.println("^ new"); // logging
                    peerListChanged++;       // A duplicate was not found on this, the peer list was changed
                }
            }
            else
            {
                Serial.println("Broadcast address received");
            }
        }
        else
        {
            Serial.println("Last address");
            break;
        }
    }
    Serial.print("My mac included? ");
    Serial.println(myMacIncluded);
    if (myMacIncluded == 0)
    {
        pushNewMacAddress(g_peers, OWN_MAC_ADDRESS);
        peerListChanged++;
    }
    if (peerListChanged == 0)
    {
        Serial.println("Peer List Confirmed!");
    }
    else
    {
        Serial.print(peerListChanged);
        Serial.println(" new peer(s) added");
    }
    printMacAddresses(g_peers);
    Serial.println("");
}

/********************************************************************************************************************************************
 *                           Callbacks
 ********************************************************************************************************************************************/

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus)
{
    char macStr[18];
    Serial.print("Packet to:");
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.print(macStr);
    Serial.print(" send status: ");
    if (sendStatus == 0)
    {
        Serial.println("Delivery success");
        g_lastDeliveryFailed = 0;
    }
    else
    {
        Serial.println("Delivery fail");
        g_lastDeliveryFailed = 1;
    }
}

// Callback function that will be executed when data is received
void OnDataRecvd(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
    autosync_send_struct receiving;
    memcpy(&receiving, incomingData, sizeof(receiving));
    Serial.println("Recieving...");
    Serial.print("Bytes received: ");
    Serial.println(len);
    Serial.print("Purpose received: ");
    Serial.println(receiving.purpose);
    Serial.print("Resend: ");
    Serial.println(receiving.resend);
    Serial.print("Address received: ");
    printMacAddress(receiving.address);
    Serial.println();

    switch (receiving.purpose)
    {
    case 1: // I'm syncing and this is my MAC address
        if (g_syncStarted != 0)
        { // If this device is also syncing
            checkAndSyncAddress(receiving);
        }
        break;
    case 2: // This is the list of peers that I have
        confirmPeerList(receiving);
        Serial.print("SyncStarted: ");
        Serial.println(g_syncStarted);
        break;

    default:
        break;
    }
    Serial.println("Finished receiving data");
}

/********************************************************************************************************************************************
 *                           Setup
 ********************************************************************************************************************************************/

void setup()
{
    // Init Serial Monitor
    Serial.begin(115200);
    Serial.println("autoSync Starting...");

    // set pins
    pinMode(SYNC_BUTTON, INPUT);
    Serial.println("Pins set");

    // Get own mac address and store in OWN_MAC_ADDRESS
    WiFi.macAddress(OWN_MAC_ADDRESS);
    Serial.print("Mac address: ");
    printMacAddress(OWN_MAC_ADDRESS);
    Serial.println(" ");

    // Set device as a Wi-Fi Station
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.println("Wifi init");

    // Init ESP-NOW
    Serial.print("ESP-NOW initialized with exit code ");
    Serial.println(esp_now_init());

    // Set role to combo, will be both sending and receiving
    Serial.println("Role set with exit code ");
    Serial.println(esp_now_set_self_role(ESP_NOW_ROLE_COMBO));

    // Register a callback function when data received
    Serial.print("Receive cb registered with exit code ");
    Serial.println(esp_now_register_recv_cb(OnDataRecvd));

    // Register a callback function when data sent
    Serial.print("Send cb registered with exit code ");
    Serial.println(esp_now_register_send_cb(OnDataSent));

    // Register the broadcast peer
    Serial.print("Peer added with exit code ");
    Serial.println(esp_now_add_peer(BROADCAST_ADDRESS, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0));
}

/********************************************************************************************************************************************
 *                           Loop
 ********************************************************************************************************************************************/

void loop()
{
    /********************************************************************************************************************************************
     *                           Initial Sync
     ********************************************************************************************************************************************/

    while (g_ownPeerListConfirmed == 0)
    {
        yield();
        if (digitalRead(SYNC_BUTTON) != 0) // Sync button held down
        {
            if (g_syncStarted == 0) // Sync has not started
            {
                g_syncStarted = 1; // Start the sync
                digitalWrite(BUILTIN_LED, LOW);
                g_startSyncTime = millis(); // mark the time the sync started
            }
            else
            {
                if ((millis() - g_startSyncTime) > 500 && g_startSyncTime > 0) // every half second after the sync starts
                {
                    g_startSyncTime = millis();
                    Serial.println("Broadcasting Mac address...");
                    sendMacAddress(); // send this unit's MAC address to everyone else (who's syncing)
                }
            }
        }
        else // Sync button is not held down
        {
            if (g_syncStarted == 1) // If sync is currently running, run this once
            {
                g_syncStarted = 2; // Sync is ending
                digitalWrite(BUILTIN_LED, HIGH);
                delay(10);                    // Don't crowd the channel
                switchFromBroadcastToPeers(); // Remove the broadcast peer and register the list of peers
                confirmSync();                // Send a copy of my peer list to my peers
                g_startSyncTime = millis();   // reset the g_startSyncTime
                Serial.println("Peer list finally confirmed");

                // Delay 3 seconds to let everyone else catch up
                for (int i = 0; i < 3; i++)
                {
                    digitalWrite(BUILTIN_LED, LOW);
                    delay(500);
                    digitalWrite(BUILTIN_LED, HIGH);
                    delay(500);
                }
                sortMacAddressArrayList(g_peers); // Sort so that everyone has the same list in the same order
                printMacAddresses(g_peers);
                Serial.println("");
                Serial.println("Devices synced and switched from broadcast mode.");
                break;
            }
        }
    }

    while (1 == 1)
    {
        yield();
        Serial.println("Done syncing! This is your new loop to do something new");
        Serial.println("Now your peer list is only those devices that have synced.");
        Serial.println("Consider using LEDs to indicate which devices have synced and which have failed");
        Serial.println("");
        delay(10000);
    }
}