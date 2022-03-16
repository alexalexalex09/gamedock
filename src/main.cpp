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

/*
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp-now-one-to-many-esp8266-nodemcu/

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

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
 * The default `2` is the blue LED on ESP-12E.
 */
static const int BUILTINLED = 2;

/**
 * @brief PIN number of the NodeMcu ESP-12E board's extra LED
 *
 */
static const int NODEMCU_LED = 16;

/**
 * @brief Activity indicator LED wired up to GPIO 5
 *
 */
static const int ACTIVITY_LED = 5;

/**
 * @brief Standard Wifi channel for all devices
 *
 * Initially using channel 9 to see how it goes
 */
static const int WIFI_CHANNEL = 1;

/**
 * @brief variable to track sync button status, 0=unpressed
 *
 *
 */
int syncButtonState = 0;

/**
 * @brief variable to track sync status, 0=off
 *
 *
 */
int syncStarted = 0;

/**
 * @brief to show if the last broadcast was not sent
 *
 */
int lastDeliveryFailed = -1;

// address used for broadcasting via espnow
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

//
uint8_t ownMacAddress[6];

// Structure example to send data
// Must match the receiver structure
/**
 *
 * command:
 * 1: Begin sync
 * 2: End sync
 *
 */
typedef struct gamedock_struct
{
  int command;
} gamedock_struct;

// Create a struct_message called sending to store variables to be sent
gamedock_struct sending;

// Create a struct_message called receiving

gamedock_struct lastSentPacket;

/**
 * @brief send a gamedock_struct to all peers
 *
 */
gamedock_struct sendPacket(gamedock_struct toSend)
{
  // Send message via ESP-NOW
  Serial.print("Message sending: command:");
  Serial.println(toSend.command);
  esp_now_send(0, (uint8_t *)&toSend, sizeof(toSend));
  return toSend; // return this to be used again in case of failure
}
void printMacAddress(uint8_t *mac_addr)
{
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

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
    lastDeliveryFailed = 0;
  }
  else
  {
    Serial.println("Delivery fail");
    lastDeliveryFailed = 1;
  }
}

// Callback function that will be executed when data is received
// Callback function that will be executed when data is received
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  gamedock_struct receiving;
  memcpy(&receiving, incomingData, sizeof(receiving));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("Command received: ");
  Serial.println(receiving.command);

  switch (receiving.command)
  {
  case 1:
    digitalWrite(ACTIVITY_LED, HIGH);
    break;
  case 2:
    digitalWrite(ACTIVITY_LED, LOW);
    break;

  default:
    break;
  }
}

void doNothing()
{
}

void setup()
{
  // Init Serial Monitor
  Serial.begin(115200);
  Serial.println("broadcast-test");

  // set pins
  pinMode(SYNC_BUTTON, INPUT);
  pinMode(BUILTINLED, OUTPUT);
  digitalWrite(BUILTINLED, HIGH);
  pinMode(NODEMCU_LED, OUTPUT);
  digitalWrite(NODEMCU_LED, HIGH);
  pinMode(ACTIVITY_LED, OUTPUT);
  Serial.println("Pins set");

  // Get own mac address and store in ownMacAddress
  WiFi.macAddress(ownMacAddress);
  Serial.print("Mac address: ");
  printMacAddress(ownMacAddress);
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
  Serial.println(esp_now_register_recv_cb(OnDataRecv));

  // Register a callback function when data sent
  Serial.print("Send cb registered with exit code ");
  Serial.println(esp_now_register_send_cb(OnDataSent));

  // Register the broadcast peer
  Serial.print("Peer added with exit code ");
  Serial.println(esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0));
}

void loop()
{
  syncButtonState = digitalRead(SYNC_BUTTON);
  if (syncButtonState != 0) // Sync button held down
  {
    if (syncStarted == 0) // Sync has not started
    {
      syncStarted = 1;                      // Mark sync as started
      gamedock_struct sending;              // create a packet to send
      sending.command = 1;                  // Set command to 1 = start sync
      lastSentPacket = sendPacket(sending); // Send the packet
    }
    else
    {
      doNothing();
    }
  }
  else // Sync button is not held down
  {
    if (syncStarted == 0)
    { // Sync has already ended
      doNothing();
    }
    else
    {                                       // Sync has not yet ended
      syncStarted = 0;                      // Mark sync as ended
      gamedock_struct sending;              // create a packet to send
      sending.command = 2;                  // set command to 2 = stop sync
      lastSentPacket = sendPacket(sending); // Send the packet
    }
  }
  if (lastDeliveryFailed == 1) // If a send failure was detected, stop execution for 50ms and resend
  {
    delay(50);
    lastDeliveryFailed = -1; // Reset variable to indicate the failure was noticed
    sendPacket(lastSentPacket);
  }
}