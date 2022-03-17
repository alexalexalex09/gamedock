/*
//1. Sync button is pressed
//2. Send broadcast signal with code for synchronization: "GameDockSync"+MAC
//3. Alternate listening and sending (with randomization to make sure everyone gets heard?)
//4. When another sync code is received, add it to the syncing list in order of MAC address
//5. Display current position in the syncing list on the device
//6. Add each MAC address as a peer
//7. When the sync button is released, broadcast the full list of MAC addresses
//7. Listen for 60 seconds to broadcast messages.
//8.   If the current device's address is in a broadcasted list of addresses, make sure each other address is in the current player list
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
#include <vector> // Needed for a dynamically-allocated peer array

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

// variable for holding this device's mac address
uint8_t ownMacAddress[6];

// Structure example to send data
// Must match the receiver structure
/**
 *
 * purpose:
 * 1: I'm syncing and this is my MAC address
 * 2: This is the list of peers that I have
 * 3: I've received no new peers from my peers
 * 10: Turn on your LED!
 * 20: Turn off your LED!
 *
 */
typedef struct gamedock_send_struct
{
  int purpose;
  uint8_t address;
  std::vector<gamedock_peer_struct> peers;
  int peerListConfirmed = 0;
} gamedock_send_struct;

/**
 * Structure to store a received gamedock peer
 *
 */
typedef struct gamedock_peer_struct
{
  uint8_t address;
} gamedock_peer_struct;

// Create the global vector of peers, max 19
std::vector<gamedock_peer_struct> peers;

// Create a struct_message called sending to store variables to be sent
gamedock_send_struct sending;

// Create a struct_message called receiving

gamedock_send_struct lastSentPacket;

/**
 * @brief send a gamedock_send_struct to all peers
 *
 */
gamedock_send_struct sendPacket(auto toSend)
{
  // Send message via ESP-NOW
  Serial.print("Message sending: command:");
  Serial.println(toSend.purpose);
  esp_now_send(0, (uint8_t *)&toSend, sizeof(toSend));
  return toSend; // return this to be used again in case of failure
}

/**********************************************************************
 *                           Functions
 **********************************************************************/

/**
 * Print a mac address out to serial
 *
 */
void printMacAddress(uint8_t *mac_addr)
{
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

// For testing only, tells other devices to turn on their LED
void turnOnYourLED()
{
  gamedock_send_struct sending;         // create a packet to send
  sending.purpose = 10;                 // Set purpose to 10 = turn on your LED!
  lastSentPacket = sendPacket(sending); // Send the packet
}

// For testing only, tells other devices to turn off their LED
void turnOffYourLED()
{
  gamedock_send_struct sending;         // create a packet to send
  sending.purpose = 20;                 // set purpose to 20 = turn off your LED!
  lastSentPacket = sendPacket(sending); // Send the packet
}

void checkAndSyncAddress(gamedock_send_struct incomingAddress)
{
  int duplicatePeer = 0;                     // Initialize a duplicate indicator
  for (gamedock_peer_struct el_peer : peers) // iterate over the vector of peers
  {
    if (el_peer.address == incomingAddress.address) // check each el_peer to see if its address is the incoming address
    {
      duplicatePeer = 1; // if it's duplicate, set duplicate to 1 (true)
      break;             // no need to keep searching, we know it's a duplicate
    }
  }
  if (duplicatePeer == 0) // after iterating, if no duplicate was detected
  {
    gamedock_peer_struct newPeer;              // create a new peer
    newPeer.address = incomingAddress.address; // assign it the incoming address
    peers.push_back(newPeer);                  // push it to the vector
  }
}

/**
 * @brief Send this device's mac address to the broadcast peer
 *
 *
 */
void broadcastMacAddress()
{
  gamedock_send_struct sending;          // Create a packet to send
  sending.purpose = 1;                   // set purpose to 1 = I'm syncing and this is my Mac address
  sending.address = *ownMacAddress;      // Include the mac address of this device. ownMacAddress is a pointer, so we need to access the contents of the location it points to
  Serial.print("Sending own address: "); // Testing
  Serial.println(sending.address);       // Testing
  Serial.println(WiFi.macAddress());     // Testing
  lastSentPacket = sendPacket(sending);  // Send the packet
}

void confirmSync()
{
  gamedock_send_struct sending;         // Create a packet to send
  sending.peers = peers;                // Attach the full list of peers
  sending.purpose = 2;                  // 2: this is the list of peers I have
  lastSentPacket = sendPacket(sending); // Send the packet
}

void reportSyncConfirmed()
{
  gamedock_send_struct sending;         // Create a packet to send
  sending.purpose = 3;                  // 3: All of my peers have confirmed they have my list
  lastSentPacket = sendPacket(sending); // Send the packet
}

void confirmPeerList(gamedock_send_struct incomingPeers)
{
  int peerListChanged = 0;                                         // initialize an indicator for whether the peer list has changed
  for (gamedock_peer_struct el_incomingPeer : incomingPeers.peers) // Loop through the incoming peers
  {
    int duplicatePeer = 0;                     // initialize a duplicate indicator
    for (gamedock_peer_struct el_peer : peers) // for each incoming peer, loop through the local list of peers
    {
      if (el_peer.address == el_incomingPeer.address)
      {
        duplicatePeer = 1; // if the incoming address equals a local address, it's a duplicate
        break;             // if so, no need to further analyze, move on to the next address
      }
    }
    if (duplicatePeer == 0) // after iterating, if no duplicate was detected
    {
      gamedock_peer_struct newPeer;              // create a new peer
      newPeer.address = el_incomingPeer.address; // assign it the incoming address
      peers.push_back(newPeer);                  // push it to the vector
      peerListChanged = 1;                       // There was a change to the peer list
    }
  }
  if (peerListChanged == 1)
  {
    confirmSync();
  }
  else
  {
    reportSyncConfirmed();
  }
}

/**********************************************************************
 *                           Callbacks
 **********************************************************************/

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
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  gamedock_send_struct receiving;
  memcpy(&receiving, incomingData, sizeof(receiving));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("Command received: ");
  Serial.println(receiving.purpose);

  switch (receiving.purpose)
  {
  case 1:
    checkAndSyncAddress(receiving);
    break;
  case 2:
    confirmPeerList(receiving);
    break;
  case 10:
    digitalWrite(ACTIVITY_LED, HIGH);
    break;
  case 20:
    digitalWrite(ACTIVITY_LED, LOW);
    break;

  default:
    break;
  }
}

void doNothing()
{
}

/**********************************************************************
 *                           Main
 **********************************************************************/

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
  unsigned long startSyncTime;
  syncButtonState = digitalRead(SYNC_BUTTON);
  if (syncButtonState != 0) // Sync button held down
  {
    if (syncStarted == 0) // Sync has not started
    {
      syncStarted = 1;          // Start the sync
      turnOnYourLED();          // Tell others to turn on their LEDs for fun
      startSyncTime = millis(); // mark the time the sync started
    }
    else
    {
      if (millis() - startSyncTime > 500 && startSyncTime > 0) // every half second after the sync starts
      {
        broadcastMacAddress(); // send this unit's MAC address to everyone else (who's syncing)
      }
    }
  }
  else // Sync button is not held down
  {
    if (syncStarted == 0) // Sync has already ended
    {
      doNothing();
    }
    else // Sync has not yet ended
    {
      if (syncStarted == 1) // If sync is currently running
      {
        syncStarted = 2;          // Sync is ending
        turnOffYourLED();         // Tell others to turn off their LEDs for fun
        confirmSync();            // Send a copy of my peer list to everyone else
        startSyncTime = millis(); // reset the startSyncTime
      }
      else // Sync is already ending
      {
        if (syncStarted < 7) // confirm the list 5 times over just to be sure all changes propagate through the system
        {
          if (millis() - startSyncTime > 250) // wait .25 seconds between each confirmation
          {
            confirmSync();            // Make sure no major changes have happened
            syncStarted++;            // increment syncStarted
            startSyncTime = millis(); // reset the startSyncTime
          }
        }
        else
        {
          syncStarted = 0; // after 5 iterations, end the cycle and set syncStarted back to 0
        }
      }
    }
  }
  if (lastDeliveryFailed == 1) // If a send failure was detected, stop execution for 50ms and resend
  {
    delay(50);
    lastDeliveryFailed = -1; // Reset variable to indicate the failure was noticed
    sendPacket(lastSentPacket);
  }
}