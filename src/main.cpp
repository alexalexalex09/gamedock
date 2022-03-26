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

// initialize a time tracking variable
unsigned long startSyncTime = 0;

/**
 * @brief to show if the last broadcast was not sent
 *
 */
int lastDeliveryFailed = -1;

// address used for broadcasting via espnow
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// variable for holding this device's mac address
uint8_t ownMacAddress[6];

// variable to hold a dummy address (all zeroes)
uint8_t dummyAddress[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// variable for showing whether the peer list has been confirmed
int ownPeerListConfirmed = 0;

/**
 * Structure to store a received gamedock peer
 *
 */
typedef struct gamedock_peer_struct
{
  uint8_t address[6];
} gamedock_peer_struct;

// Create the global vector of peers, max 19
std::vector<gamedock_peer_struct> peers;

// Structure example to send data
// Must match the receiver structure
/**
 *
 * uint8_t address[6]
 * std::vector<gamedock_peer_struct> peers
 * int peerListConfirmed = 0
 * int purpose:
 * 1: I'm syncing and this is my MAC address
 * 2: This is the list of peers that I have
 * 10: Turn on your LED!
 * 20: Turn off your LED!
 *
 */
typedef struct gamedock_send_struct
{
  int purpose;
  uint8_t address[6];
  std::vector<gamedock_peer_struct> peers;
  int peerListConfirmed = 0;
} gamedock_send_struct;

// Create a struct_message called sending to store variables to be sent
gamedock_send_struct sending = {0};

// Create a struct_message called receiving
gamedock_send_struct lastSentPacket = {0};

/**
 * @brief send a gamedock_send_struct to all peers
 *
 */
gamedock_send_struct sendPacket(gamedock_send_struct toSend)
{
  // Send message via ESP-NOW
  Serial.print("Message sending: command: ");
  Serial.println(toSend.purpose);
  esp_now_send(0, (uint8_t *)&toSend, sizeof(toSend));
  return toSend; // return this to be used again in case of failure
}

/**********************************************************************
 *                           Functions
 **********************************************************************/

/**
 * @brief Print a mac address out to serial
 *
 */
void printMacAddress(uint8_t *mac_addr)
{
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

/**
 * @brief copy a mac address from param 2 to param 1
 *
 *
 */
void copyMacAddress(uint8_t *dest, uint8_t *source)
{
  for (int i = 0; i < 6; i++)
  {
    dest[i] = source[i];
  }
}

/**
 * @brief print a (vector) list of peers
 *
 *
 */
void printPeers(std::vector<gamedock_peer_struct> peersToPrint)
{
  Serial.print("Printing Peers: (");
  Serial.print(peersToPrint.size());
  Serial.println(") ");
  for (unsigned int i = 0; i < peersToPrint.size(); i++)
  {
    Serial.print(i + 1);
    Serial.print(": ");
    printMacAddress(peersToPrint[i].address);
    Serial.println();
  }
}

/**
 * @brief check if mac address are equal and return a boolean
 *
 *
 *
 */
boolean areMacAddressesEqual(uint8_t *first, uint8_t *second)
{
  Serial.println("Checking if addresses are equal");
  for (int i = 0; i < 6; i++)
  {
    if (first[i] != second[i])
    {
      Serial.println("Unequal");
      return false;
    }
  }
  Serial.println("Address was equal");
  return true;
}

/**
 * @brief check an incoming address against the global list of peers to see if it's new, and if so, add it to the list.
 *
 *
 */
void checkAndSyncAddress(gamedock_send_struct incomingAddress)
{
  int duplicatePeer = 0; // Initialize a duplicate indicator
  Serial.print("Checking address: ");
  printMacAddress(incomingAddress.address);
  Serial.println();
  for (gamedock_peer_struct el_peer : peers) // iterate over the vector of peers
  {
    if (areMacAddressesEqual(&el_peer.address[0], &incomingAddress.address[0])) // check each el_peer to see if its address is the incoming address
    {
      duplicatePeer = 1; // if it's duplicate, set duplicate to 1 (true)
      break;             // no need to keep searching, we know it's a duplicate
    }
  }
  Serial.println("Finished iteration");
  if (duplicatePeer == 0) // after iterating, if no duplicate was detected
  {
    gamedock_peer_struct newPeer; // create a new peer
    copyMacAddress(&newPeer.address[0], &incomingAddress.address[0]);
    peers.push_back(newPeer); // push it to the vector
  }
}

/**
 * @brief Send this device's mac address to the broadcast peer
 *
 *
 */
void broadcastMacAddress()
{
  gamedock_send_struct sending = {0};                     // Create a packet to send
  sending.purpose = 1;                                    // set purpose to 1 = I'm syncing and this is my Mac address
  copyMacAddress(&sending.address[0], &ownMacAddress[0]); // Include the mac address of this device. ownMacAddress is a pointer, so we need to access the contents of the location it points to
  Serial.print("Sending own address: ");                  // Logging
  printMacAddress(sending.address);                       // Logging
  Serial.println();
  Serial.println(WiFi.macAddress()); // Logging
  printPeers(sending.peers);
  Serial.println();                     // Logging
  lastSentPacket = sendPacket(sending); // Send the packet
}

/**
 * @brief Remove the broadcastAddress from the esp now peer list and adds all peers in the peers global
 *
 *
 */
void switchFromBroadcastToPeers()
{
  esp_now_del_peer(broadcastAddress);
  for (gamedock_peer_struct el_peer : peers)
  {
    esp_now_add_peer(el_peer.address, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0);
  }
}

/**
 * @brief Remove the peers in the peers global from the esp now peer list and adds the broadcastAddress
 *
 *
 */
void switchFromPeersToBroadcast()
{
  for (gamedock_peer_struct el_peer : peers)
  {
    esp_now_del_peer(el_peer.address);
  }
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0);
}

/**
 * @brief Send the list of peers out to all currently registered peers
 *
 */
void confirmSync()
{
  Serial.println("Confirming sync..."); // logging
  gamedock_send_struct sending = {0};   // Create a packet to send
  sending.peers = peers;                // Attach the full list of peers
  sending.peerListConfirmed = 0;
  copyMacAddress(&sending.address[0], &dummyAddress[0]); // Send a dummy address
  Serial.print("Mac to send: ");
  printMacAddress(&sending.address[0]);
  Serial.println();
  sending.purpose = 2;                  // 2: this is the list of peers I have
  lastSentPacket = sendPacket(sending); // Send the packet
  printPeers(peers);
}

/**
 * @brief Given a struct with a peers parameter, push it to the global peers vector if it's new
 *
 *
 */
void confirmPeerList(gamedock_send_struct incomingPeers)
{
  // int peerListChanged = 0;                                         // initialize an indicator for whether the peer list has changed
  for (gamedock_peer_struct el_incomingPeer : incomingPeers.peers) // Loop through the incoming peers
  {
    int duplicatePeer = 0;                     // initialize a duplicate indicator
    Serial.println("Checking MAC addresses:"); // Logging
    printMacAddress(el_incomingPeer.address);  // Logging
    if (areMacAddressesEqual(&el_incomingPeer.address[0], &dummyAddress[0]))
    {
      Serial.println("Dummy address received");
    }
    else
    {
      for (gamedock_peer_struct el_peer : peers) // for each incoming peer, loop through the local list of peers
      {

        if (el_peer.address == el_incomingPeer.address)
        {
          duplicatePeer = 1;             // if the incoming address equals a local address, it's a duplicate
          Serial.println(": duplicate"); // logging
          printMacAddress(el_peer.address);
          Serial.println();
          printMacAddress(el_incomingPeer.address);
          Serial.println();
          break; // if so, no need to further analyze, move on to the next address
        }
      }
      if (duplicatePeer == 0) // after iterating, if no duplicate was detected
      {
        Serial.println(": new");      // logging
        gamedock_peer_struct newPeer; // create a new peer
        Serial.println("Assigning...");
        copyMacAddress(&newPeer.address[0], &el_incomingPeer.address[0]); // assign it the incoming address
        Serial.println("Pushing...");
        peers.push_back(newPeer); // push it to the vector
        Serial.println("Peer list confirmed successfully");
        // peerListChanged = 1;                       // There was a change to the peer list
      }
    }
  }
  /*if (peerListChanged == 1)
  {
    confirmSync();
  }
  else // There were no changes to the list
  {
    if (ownPeerListConfirmed == 1) // If it's been long enough that I've already sent 5 confirmations out
    {
      ownPeerListConfirmed = 2; // Then receiving a confirmation sets this to 2. It might not happen, for instance if I'm the last one to confirm
    }
  }*/
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
  Serial.println("Recieving...");
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("Purpose received: ");
  Serial.println(receiving.purpose);
  Serial.print("Address received: ");
  printMacAddress(&receiving.address[0]);
  Serial.println();

  switch (receiving.purpose)
  {
  case 1: // I'm syncing and this is my MAC address
    if (syncStarted != 0)
    { // If this device is also syncing
      checkAndSyncAddress(receiving);
    }
    break;
  case 2: // This is the list of peers that I have
    confirmPeerList(receiving);
    Serial.print("SyncStarted: ");
    Serial.println(syncStarted);
    break;

  default:
    break;
  }
  Serial.println("Finished receiving data");
}

void doNothing()
{
}

/**********************************************************************
 *                           Setup
 **********************************************************************/

void setup()
{
  // Init Serial Monitor
  Serial.begin(115200);
  Serial.println("gamedock");

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

/**********************************************************************
 *                           Loop
 **********************************************************************/

void loop()
{

  /**********************************************************************
   *                           Sync Button
   **********************************************************************/

  syncButtonState = digitalRead(SYNC_BUTTON); // get the physical sync button's state
  if (syncButtonState != 0)                   // Sync button held down
  {
    if (syncStarted == 0) // Sync has not started
    {
      syncStarted = 1; // Start the sync
      digitalWrite(ACTIVITY_LED, HIGH);
      startSyncTime = millis(); // mark the time the sync started
    }
    else
    {
      if ((millis() - startSyncTime) > 500 && startSyncTime > 0) // every half second after the sync starts
      {
        startSyncTime = millis();
        Serial.println("Broadcasting Mac address...");
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
        syncStarted = 2; // Sync is ending
        digitalWrite(ACTIVITY_LED, LOW);
        delay(10);                    // Don't crowd the channel
        switchFromBroadcastToPeers(); // Remove the broadcast peer and register the list of peers
        confirmSync();                // Send a copy of my peer list to my peers
        startSyncTime = millis();     // reset the startSyncTime
      }
      else // Sync is already ending
      {
        /*if (syncStarted < 7) // confirm the list 5 times over just to be sure all changes propagate through the system
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
        confirmSync(); // Make sure no major changes have happened
        */
        Serial.println("Peer list finally confirmed");
        syncStarted = 0;          // after 5 iterations, end the cycle and set syncStarted back to 0
        ownPeerListConfirmed = 1; // This is as good as it gets! 1 = I've sent 5, receive callback will set this to 2 assuming I'm not the last one to confirm
        /*}*/
      }
    }
  }

  /**********************************************************************
   *                           Turn Button
   **********************************************************************/

  /**********************************************************************
   *                           Send Failure Catchall
   **********************************************************************/
  if (lastDeliveryFailed == 1) // If a send failure was detected, stop execution for 50ms and resend
  {
    delay(50);
    lastDeliveryFailed = -1; // Reset variable to indicate the failure was noticed
    sendPacket(lastSentPacket);
  }
}