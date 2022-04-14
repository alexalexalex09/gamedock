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
MAC Addresses:
Green: ac:0b:fb:d6:bc:73
BLue: 40:91:51:52:f0:5b
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
int g_syncButtonState = 0;

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
 * @brief the maximum number of peers we're willing to allow
 *
 */
static const int MAX_PEERS = 20;

/**
 * @brief a global vector of peers, max 20
 *
 *
 */
uint8_t g_peers[MAX_PEERS][6] = {0};

/**
 * @brief a structure to send data, which must be matched on the receiving side
 *
 * uint8_t address[6]:
 * The address that is currently being sent
 *
 * uint8_t g_peers[MAX_PEERS][6] = {0} :
 * A two dimensional array: 20 instances of 6 digit mac addresses
 *
 * int peerListConfirmed = 0
 * A flag for whether the peer list has been confirmed
 *
 * int purpose:
 * 1: I'm syncing and this is my MAC address
 * 2: This is the list of peers that I have
 * int resend = 0 to indicate if this is being resent because of a reported sending failure
 *
 */
typedef struct gamedock_send_struct
{
  int purpose;
  uint8_t address[6];
  uint8_t peers[MAX_PEERS][6] = {0};
  int peerListConfirmed = 0;
  int resend = 0;
} gamedock_send_struct;

// Create a struct_message called sending to store variables to be sent
gamedock_send_struct sending = {0};

// Create a struct_message called receiving
gamedock_send_struct lastSentPacket = {0};

/********************************************************************************************************************************************
 *                           Functions
 ********************************************************************************************************************************************/

/**
 * @brief Print a mac address out to serial
 *
 */
void printMacAddress(uint8_t mac_addr[6])
{
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

/**
 * @brief check if mac address are equal and return a boolean
 *  Runs through all 6 digits to check if each is equal
 *
 *
 */
boolean areMacAddressesEqual(uint8_t first[6], uint8_t second[6])
{
  for (int i = 0; i < 6; i++)
  {
    if (first[i] != second[i])
    {
      return false;
    }
  }
  return true;
}

/**
 * @brief send a gamedock_send_struct to all peers
 * Depends on printMacAddress
 *
 *
 */
gamedock_send_struct sendPacket(gamedock_send_struct toSend)
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

/**
 * @brief copy a mac address from param 2 to param 1
 * Runs through each digit and copies it
 *
 */
void copyMacAddress(uint8_t dest[], uint8_t source[], int size = 6)
{
  for (int i = 0; i < size; i++)
  {
    dest[i] = source[i];
  }
}

/**
 * @brief print a list of peers from a two dimensional array that must be as long as MAX_PEERS
 *  Depends on areMacAddressesEqual and printMacAddress
 *
 *  This iterates through an entire peers list and prints each peer until it comes to one that is all zeroes
 *
 */
void printPeers(uint8_t peersToPrint[MAX_PEERS][6])
{
  Serial.println("Printing Peers:");
  for (unsigned int i = 0; i < MAX_PEERS; i++)
  {
    if (!areMacAddressesEqual(peersToPrint[i], DUMMY_ADDRESS))
    {
      Serial.print(i + 1);
      Serial.print(": ");
      printMacAddress(peersToPrint[i]);
      Serial.println();
    }
    else
    {
      break;
    }
  }
}

/**
 * @brief push a new peer onto the g_peers array
 * Depends on areMacAddresseEqual
 *
 * Iterates through the global g_peers to push a new peer onto the first slot that is all zeroes
 *
 */
void pushNewPeer(uint8_t newPeer[6])
{
  for (int i = 0; i < MAX_PEERS; i++)
  {
    if (areMacAddressesEqual(g_peers[i], DUMMY_ADDRESS))
    {
      Serial.println("Copying...");
      for (int j = 0; j < 6; j++)
      {
        g_peers[i][j] = newPeer[j];
        Serial.print(newPeer[j]);
        Serial.print(":");
      }
      Serial.println("");
      break;
    }
  }
}

/**
 * @brief remove a peer, then move subsequent array items forward to prevent empty spaces
 * Depends on areMacAddressesEqual
 *
 * @param peerNumber the index from which to remove an item, cannot be higher than MAX_PEERS - 1
 *
 *
 */
void removePeer(int peerNumber)
{
  if (peerNumber > MAX_PEERS - 1)
  {
    Serial.print("****WARNING! PEER NOT REMOVED! PARAMETER TOO HIGH: ");
    Serial.println(peerNumber);
  }
  else
  {
    // Remove the identified peer
    for (int i = 0; i < 6; i++)
    {
      g_peers[peerNumber][i] = 0x00;
    }
    // Look at the next peer
    peerNumber++;
    // Iterate through the remaining peers (if it was 19, it won't interate)
    for (int i = peerNumber; i < MAX_PEERS; i++)
    {
      // If the current address isn't 0
      if (!areMacAddressesEqual(g_peers[i], DUMMY_ADDRESS))
      {
        // copy the current address into the previous (empty) address
        for (int j = 0; j < 6; j++)
        {
          g_peers[i - 1][j] = g_peers[i][j];
        }
        // empty the current address
        for (int j = 0; j < 6; j++)
        {
          g_peers[i + 1][j] = 0x00;
        }
      }
      else
      {
        // if the next address was 0, we've reached the end of non-empty addresses
        break;
      }
    }
  }
}

/**
 * @brief check an incoming address against the global list of peers to see if it's new, and if so, add it to the list.
 *  Depends on areMacAddressesEqual and pushNewPeer
 *
 *  @param incomingAddress a gamedock_send_struct to check
 *
 *
 *
 */
void checkAndSyncAddress(gamedock_send_struct incomingAddress)
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
      pushNewPeer(incomingAddress.address); // push it to the array
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
  gamedock_send_struct sending = {0};               // Create a packet to send
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
  esp_now_del_peer(BROADCAST_ADDRESS);
  for (int i = 0; i < MAX_PEERS; i++)
  {
    esp_now_add_peer(g_peers[i], ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0);
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
    esp_now_del_peer(g_peers[i]);
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
  Serial.println("Confirming sync..."); // logging
  delay(100);
  gamedock_send_struct sending = {0}; // Create a packet to send
  copyPeers(sending.peers, g_peers);  // Attach the full list of peers
  sending.peerListConfirmed = 0;
  copyMacAddress(sending.address, DUMMY_ADDRESS); // Send a dummy address
  sending.purpose = 2;                            // 2: this is the list of peers I have
  Serial.println("Confirming Sync:");
  Serial.print("Mac: ");
  printMacAddress(sending.address); // Todo: What the heck address is being sent??
  Serial.println();
  Serial.print("Peer list confirmed: ");
  Serial.println(sending.peerListConfirmed);
  Serial.print("Purpose: ");
  Serial.println(sending.purpose);
  Serial.print("Resend: ");
  Serial.println(sending.resend);
  Serial.print("Peers to send: ");
  printPeers(sending.peers);
  Serial.println("");
  lastSentPacket = sendPacket(sending); // Send the packet
  Serial.println("Local peers:");
  printPeers(g_peers);
}

/**
 * @brief Given a struct with a peers parameter, push it to the global peers vector if it's new
 *
 *
 */
void confirmPeerList(gamedock_send_struct incomingPeers)
{
  // int peerListChanged = 0;                                         // initialize an indicator for whether the peer list has changed
  /*for (gamedock_peer_struct el_incomingPeer : incomingPeers.peers) // Loop through the incoming peers
  {
    int duplicatePeer = 0;                     // initialize a duplicate indicator
    Serial.println("Checking MAC addresses:"); // Logging
    printMacAddress(el_incomingPeer.address);  // Logging
    Serial.println();
    printMacAddress(dummyAddress);
    Serial.println();
    if (areMacAddressesEqual(el_incomingPeer.address, dummyAddress))
    {
      Serial.println("Dummy address received");
    }
    else
    {
      if (areMacAddressesEqual(el_incomingPeer.address, BROADCAST_ADDRESS))
      {
        Serial.println("Broadcast address received");
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
          Serial.print("Assigning ");
          printMacAddress(el_incomingPeer.address);
          Serial.println();
          copyMacAddress(newPeer.address, el_incomingPeer.address); // assign it the incoming address
          Serial.println("Pushing ");
          printMacAddress(newPeer.address);
          Serial.println();
          g_peers.push_back(newPeer); // push it to the vector
          Serial.println("Peer list confirmed successfully");
        }
      }
    }
  }*/
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
  gamedock_send_struct receiving;
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

void doNothing()
{
}

/********************************************************************************************************************************************
 *                           Setup
 ********************************************************************************************************************************************/

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
   *                           Sync Button
   ********************************************************************************************************************************************/

  g_syncButtonState = digitalRead(SYNC_BUTTON); // get the physical sync button's state
  if (g_syncButtonState != 0)                   // Sync button held down
  {
    if (g_syncStarted == 0) // Sync has not started
    {
      g_syncStarted = 1; // Start the sync
      digitalWrite(ACTIVITY_LED, HIGH);
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
    if (g_syncStarted == 0) // Sync has already ended
    {
      doNothing();
    }
    else // Sync has not yet ended
    {
      if (g_syncStarted == 1) // If sync is currently running
      {
        g_syncStarted = 2; // Sync is ending
        digitalWrite(ACTIVITY_LED, LOW);
        delay(10);                  // Don't crowd the channel
                                    // switchFromBroadcastToPeers();          // Remove the broadcast peer and register the list of peers
        confirmSync();              // Send a copy of my peer list to my peers
        g_startSyncTime = millis(); // reset the g_startSyncTime
      }
      else // Sync is already ending
      {
        Serial.println("Peer list finally confirmed");
        g_syncStarted = 0;          // after 5 iterations, end the cycle and set g_syncStarted back to 0
        g_ownPeerListConfirmed = 1; // This is as good as it gets! 1 = I've sent 5, receive callback will set this to 2 assuming I'm not the last one to confirm
      }
    }
  }

  /********************************************************************************************************************************************
   *                           Turn Button
   ********************************************************************************************************************************************/

  /********************************************************************************************************************************************
   *                           Send Failure Catchall
   ********************************************************************************************************************************************/
  if (g_lastDeliveryFailed == 1) // If a send failure was detected, stop execution for 50ms and resend
  {
    delay(50);
    g_lastDeliveryFailed = 0; // Reset variable to indicate the failure was noticed
    lastSentPacket.resend = 1;
    sendPacket(lastSentPacket);
  }
}