/*

//1. Sync button is pressed
//2. Send broadcast signal with code for synchronization: "GameDockSync"+MAC
//3. Alternate listening and sending (with randomization to make sure everyone gets heard?)
//4. When another sync code is received, add it to the syncing list in order of MAC address
//5. Display current position in the syncing list on the device
//6. Add each MAC address as a peer
//7. When the sync button is released, broadcast the full list of MAC addresses
//7. Listen for 60 seconds to broadcast messages.
//    If the current device's address is in a broadcasted list of addresses, make sure each other address is in the current player list
//9. Set the current turn to peer 1
//10. If this device is currently taking a turn, send an "I'm taking a turn" ping every second
//10a. If not taking a turn, listen for the "I'm taking a turn ping". If not heard for over 5 seconds, advance the current player
//11. When the current player presses the "next" or "previous" button, a "SetPlayerTurn"+NextPlayer message is sent to all peers.
//12. When a device stops taking a turn, set that device to inactive and skip it.
//12a. When receiving a "reactivate"+PlayerNumber message, set that device to active and include it in the rotation
//13. If the sync button is held down while a session is active, erase the session

//Possible problems:
//1) Player 1 runs out of battery. In this case, set player 1 to inactive and switch to player 2 after not hearing player 1 ping for 5 second
//2) Player 1 puts a new battery back in after running out of battery.
//    In this case, listen for the "I'm taking a turn" ping and set that player to current player. Then send a "reactivate"+PlayerNumber message
//    If not found, erase the game and go back to holding mode, the game ended
//3) Not every device heard the full list of players. Each device will broadcast a list of players for 60 seconds after the sync button is released to be sure all are included
//4) Player 4 runs out of battery. In this case, nothing happens until it's player 4's turn
//    When player 3 presses the next button, all devices will set the player turn to 4.
//    After 5 seconds, player 4 is not found and set to inactive, and then the current player is advanced
*/

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <stdbool.h>

/**
 * @brief PIN number of the sync button.
 *
 * For a momentary switch hooked up to GPIO 4
 */
static const int SYNC_BUTTON = 4;

/**
 * @brief PIN number of a button.
 *
 * The default `0` is the "flash" button on NodeMCU, Witty Cloud, Heltec WiFi_Kit_32, etc.
 */
static const int FLASH_BUTTON = 0;

/**
 * @brief PIN number of an LED.
 *
 * The default `16` is the blue LED on ESP-12E.
 */
static const int BUILTINLED = 16;

/**
 * @brief Standard Wifi channel for all devices
 *
 * Initially using channel 9 to see how it goes
 */
static const int WIFI_CHANNEL = 9;

/**
 * @brief Sync button current state
 *
 */
int syncButtonState = 0;

/**
 * @brief Whether or not the device is currently in sync mode
 *
 *  Boolean, default to false, needs #include <stdbool.h>
 */
bool syncing = false;

/**
 * @brief broadcast address
 *
 */
static const uint8_t BROADCAST_PEER[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/**
 * @brief MAC address
 *
 * Address of the device
 */
static const uint8_t MACADDRESS = WiFi.macAddress();
void setup()

    /**
     * @brief Custom struct for communication between devices
     *
     * @param MAC contains the sending device mac address
     * @param syncing is a bool, needs #include <stdbool.h>
     */
    typedef struct sync_struct
{
    uint8_t[6] MAC;
    bool syncing;
} sync_struct;

{
    // Set appropriate bit rate
    Serial.begin(115200);
    // Not sure why wifi needs to be in STA mode yet
    WiFi.mode(WIFI_STA);
    Serial.println();
    Serial.print("ESP8266 Board MAC Address:  ");
    Serial.println(WiFi.macAddress());

    // Initialize ESPNow
    if (esp_now_init() != 0)
    {
        Serial.println("Problem during ESP-NOW init");
        return;
    }

    // Set up peers
    // https://techtutorialsx.com/2019/10/20/esp32-getting-started-with-esp-now/
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, BROADCAST_PEER, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer");
        return;
    }

    // Set pins
    pinMode(SYNC_BUTTON, INPUT);
    pinMode(BUILTINLED, OUTPUT);
}

void loop()
{
    syncButtonState = digitalRead(SYNC_BUTTON);
    if (syncButtonState != 0) // Sync button is held down
    {
        if (syncing == false) // Not already syncing
        {
            beginSync();
        }
        else // already syncing
        {
            continueSync();
        }
    }
    if (syncButtonState == 0) // Sync button is not held down
    {
        if (syncing == true) // Syncing has started
        {
            endSync();
        }
        else
        {
            doNothing() // No syncing and no button press
        }
    }

    testBroadcast();
    delay(1000);
}

/**
 * @brief Begin the sync process
 *
 * 1. Sync button is pressed
 * 2. Send broadcast signal with code for synchronization: "GameDockSync"+MAC
 * 3. Alternate listening and sending (with randomization to make sure everyone gets heard?)
 * 4. When another sync code is received, add it to the syncing list in order of MAC address
 * 5. Display current position in the syncing list on the device
 * 6. Add each MAC address as a peer
 */

void beginSync()
{
    syncing = true;
    sync_struct gameDockSync;
    gameDockSync.MAC = MACADDRESS;
    esp_now_send(BROADCAST_PEER, (uint8_t *) &gameDockSync, sizeof(sync_struct);
}

void testBroadcast()
{
    int x = millis();
    esp_err_t result = esp_now_send(BROADCAST_PEER, (uint8_t *)&x, sizeof(int));
    if (result == ESP_OK)
    {
        Serial.println("Sent with success");
    }
    else
    {
        Serial.println("Error sending the data");
    }
}

void continueSync()
{
}

void doNothing()
{
}