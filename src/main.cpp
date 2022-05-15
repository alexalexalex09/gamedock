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
//10. When the current player presses the "next" or "previous" button, a "SetPlayerTurn"+NextPlayer message is sent to all peers.
//11. If this device is currently taking a turn, send an "I'm taking a turn" ping every second
//11a. If not taking a turn, listen for the "I'm taking a turn ping". If not heard for over 5 seconds, advance the current player
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
Blue: 40:91:51:52:f0:5b
Yellow: ac:0b:fb:d7:b7:09
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
 * @brief PIN number of the "Previous" button
 *
 * For a momentary switch hooked up to GPIO 14, NodeMCU D5
 */
static const int PREV_BUTTON = 14;

/**
 * @brief PIN number of the "Next" button
 *
 * For a momentary switch hooked up to GPIO 12, NodeMCU D6
 */
static const int NEXT_BUTTON = 12;

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
 * @brief variable to track "previous" button status, 0=unpressed
 *
 *
 */
int g_prevButtonState = 0;

/**
 *
 * @brief variable to track when the previous button was last pressed down
 *
 */
unsigned long g_prevStart = 0;

/**
 * @brief variable to track "next" button status, 0=unpressed
 *
 *
 */
int g_nextButtonState = 0;

/**
 *
 * @brief variable to track when the next  button was last pressed down
 *
 */
unsigned long g_nextStart = 0;

/**
 * @brief flag to track whether action is still being taken from the current button press
 *
 *
 */
// int g_button_pressed = 0;

/**
 * @brief variable to track sync status, 0=off
 *
 *
 */
int g_syncStarted = 0;

/**
 * @brief a variable to track whether all players have selected their turn order
 *
 *
 */
int g_allSelected = 0;

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
 * @brief variable to store durations for pulseIn
 *
 */
unsigned long g_duration = 0;

unsigned long g_durationStart = 0;

int g_newDurationAvailable = 1;

/**
 * @brief variable used to determine whether the first player is being bothered (purpose 5)
 *
 */
int g_beingBothered = 0;

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
 * @brief the number of peers that have been synced
 *
 */
int g_syncedPeers = 0;

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
 * int indicator = 0
 * An indicator flag
 * Purpose 2: a flag for whether the peer list has been confirmed.
 * Purpose 3: the index number of the new current player

 *
 * int purpose:
 * 1: I'm syncing and this is my MAC address
 * 2: This is the list of peers that I have
 * 3: I'm setting the current player
 * 4: I'm registering my turn order
 * 5: I'm poking the current player
 * int resend = 0 to indicate if this is being resent because of a reported sending failure.
 *
 */
typedef struct autosync_send_struct
{
  int purpose;
  uint8_t address[6];
  uint8_t peers[MAX_PEERS][6] = {0};
  int indicator = 0;
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
 * @brief an interrupt function to track how long the sync button has been held down
 *
 */
IRAM_ATTR void syncInterrupt()
{
  if (digitalRead(SYNC_BUTTON) == HIGH)
  {
    Serial.println("Duration start");
    g_durationStart = millis();
    g_duration = 0;
  }
  else
  {
    Serial.println("Duration end");
    g_duration = millis() - g_durationStart;
    g_durationStart = 0;
    g_newDurationAvailable = 1;
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
void removeAddressFromArray(int peerNumber)
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
 *  Depends on areMacAddressesEqual and pushNewPeer
 *
 *  @param incomingAddress a gamedock_send_struct to check
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
  Serial.println("Confirming sync..."); // logging
  delay(100);
  autosync_send_struct sending = {0}; // Create a packet to send
  copyPeers(sending.peers, g_peers);  // Attach the full list of peers
  sending.indicator = 0;
  copyMacAddress(sending.address, DUMMY_ADDRESS); // Send a dummy address
  sending.purpose = 2;                            // 2: this is the list of peers I have
  Serial.println("Confirming Sync:");
  Serial.print("Mac: ");
  printMacAddress(sending.address); // Todo: What the heck address is being sent??
  Serial.println();
  Serial.print("Peer list confirmed: ");
  Serial.println(sending.indicator);
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
          pushNewPeer(incomingPeers.peers[i]);
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
    pushNewPeer(OWN_MAC_ADDRESS);
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
  printPeers(g_peers);
  Serial.println("");
}

int setSyncedPeers()
{
  for (int i = 0; i < MAX_PEERS; i++)
  {
    if (areMacAddressesEqual(g_peers[i], DUMMY_ADDRESS))
    {
      g_syncedPeers = i;
      return i;
    }
  }
  return g_syncedPeers;
}

int macAddressSorter(const void *cmp1, const void *cmp2)
{
  int a = 0;
  int b = 0;
  for (int i = 0; i < 6; i++)
  {
    a += ((uint8_t *)cmp1)[i];
    b += ((uint8_t *)cmp2)[i];
  }
  return b - a;
}

void sortMacAddressArrayList()
{
  g_syncedPeers = setSyncedPeers();
  Serial.print("Peers synced: ");
  Serial.println(g_syncedPeers);
  qsort(g_peers, g_syncedPeers, sizeof(g_peers[0]), macAddressSorter);
}

void checkIfCurrentPlayer()
{
  if (areMacAddressesEqual(g_currentPlayer, OWN_MAC_ADDRESS))
  {
    digitalWrite(ACTIVITY_LED, HIGH);
    Serial.print("I am the current player: ");
    printMacAddress(g_currentPlayer);
  }
  else
  {
    digitalWrite(ACTIVITY_LED, LOW);
    Serial.println("I am not the current player: ");
    printMacAddress(g_currentPlayer);
    Serial.println("");
    printMacAddress(OWN_MAC_ADDRESS);
    Serial.println("");
    for (int i = 0; i < 6; i++)
    {
      Serial.println(g_currentPlayer[i] == OWN_MAC_ADDRESS[i]);
      Serial.print(g_currentPlayer[i]);
      Serial.print(" | ");
      Serial.println(OWN_MAC_ADDRESS[i]);
    }
  }
}

int setNextPlayer()
{
  int nextPlayer = -1;
  for (int i = 0; i < g_syncedPeers; i++)
  {
    if (areMacAddressesEqual(g_currentPlayer, g_peers[i]))
    {
      if (!areMacAddressesEqual(g_peers[i + 1], DUMMY_ADDRESS))
      {
        nextPlayer = i + 1;
      }
      else
      {
        nextPlayer = 0;
      }
      break;
    }
  }
  return nextPlayer;
}

int setPrevPlayer()
{
  int nextPlayer = -1;
  for (int i = g_syncedPeers - 1; i >= 0; i--)
  {
    if (areMacAddressesEqual(g_currentPlayer, g_peers[i]))
    {
      if (i > 0)
      {
        nextPlayer = i - 1;
      }
      else
      {
        nextPlayer = g_syncedPeers - 1;
      }
      break;
    }
  }
  return nextPlayer;
}

void registerTurnOrder(uint8_t incomingAddress[6])
{
  Serial.print("Registering turn order for");
  printMacAddress(incomingAddress);
  Serial.print("at index:");
  for (int i = 0; i < g_syncedPeers; i++)
  {                                                            // Loop through the g_tempPeers list
    if (areMacAddressesEqual(g_tempPeers[i], incomingAddress)) // if the address is already registered, ignore
    {
      Serial.print(i);
      Serial.println(", and it was a duplicate");
      break;
    }
    if (areMacAddressesEqual(g_tempPeers[i], incomingAddress))
    {
      Serial.println("Why are we still executing?");
    }
    if (areMacAddressesEqual(g_tempPeers[i], DUMMY_ADDRESS)) // When you find an empty index,
    {                                                        // Copy the incoming address to that index
      Serial.println(i);
      copyMacAddress(g_tempPeers[i], incomingAddress);
      if (i == g_syncedPeers - 1) // If this is the last index, all have been selected
      {
        g_allSelected = 1;
      }
      if (i == g_syncedPeers - 2) // If we're at the index before the last one, i.e. index 1 (second slot) with 3 total players
      {                           // Then there's only one peer left to sync. We know who that is, so assign it.
        for (int j = 0; j < g_syncedPeers; j++)
        {
          int found = 0;
          for (int k = 0; k < g_syncedPeers; k++)
          {
            if (areMacAddressesEqual(g_peers[j], g_tempPeers[k])) // For each peer registered, see if they're already in the turn order list
            {
              found = 1;
              break;
            }
          }
          if (found == 0) // If found was not assigned, we know that this peer wasn't in the turn order list
          {               // So we can assign that peer to the final slot
            copyMacAddress(g_tempPeers[g_syncedPeers - 1], g_peers[j]);
            g_allSelected = 1; // And now we know that all players have been selected
            break;
          }
        }
      }
      break;
    }
  }
}

void sendAndRegisterTurnOrder(uint8_t addressToSend[6])
{
  Serial.print("Sending turn order: ");
  printMacAddress(addressToSend);
  autosync_send_struct sending = {0};
  sending.purpose = 4;
  copyMacAddress(sending.address, addressToSend);
  lastSentPacket = sendPacket(sending);
  registerTurnOrder(addressToSend);
}

/**
 * Send a packet setting a new currentPlayer
 * @param player Set this to -1 for next player, -2 for previous player, or an arbitrary player index number
 *
 *
 */
void passTurn(int player = -1)
{
  if (!areMacAddressesEqual(g_currentPlayer, OWN_MAC_ADDRESS)) // If this device isn't the current player
  {                                                            // Then ignore this button press
    // g_button_pressed = 0;
    return;
  }
  // Initialize a packet to send
  autosync_send_struct sending = {0};
  sending.purpose = 3;
  sending.indicator = -1;
  int nextPlayer = -1;
  switch (player)
  {
  case -1:
    nextPlayer = setNextPlayer();
    break;
  case -2:
    nextPlayer = setPrevPlayer();
    break;
  default:
    if (player <= g_syncedPeers) // If the player set is within the bounds of the player count,
    {                            // send the specified index
      nextPlayer = player;
    }
    else
    {
      Serial.print("Cannot set this player: ");
      Serial.println(player);
    }
    break;
  }
  if (nextPlayer != -1) // If a player has been set
  {
    sending.indicator = nextPlayer;                       // Send the index number as an indicator
    copyMacAddress(sending.address, g_peers[nextPlayer]); // Set the address to the next player's address
    copyMacAddress(g_currentPlayer, g_peers[nextPlayer]); // Set the local current player to the next player's address
    lastSentPacket = sendPacket(sending);                 // Send the packet
    checkIfCurrentPlayer();                               // Turn off the LED if this device is no longer the current player
  }
  else // The parameter was greater than the number of players or less than -2
  {
    Serial.println("Error in setting next player");
  }
  // g_button_pressed = 0;
}

void setFirstPlayer()
{
  Serial.println("");
  copyMacAddress(g_currentPlayer, g_peers[0]);
  Serial.print("My address: ");
  printMacAddress(OWN_MAC_ADDRESS);
  Serial.println("");
  if (areMacAddressesEqual(g_currentPlayer, OWN_MAC_ADDRESS)) // If I'm the lowest MAC, randomize and set the first player
  {
    Serial.print("Choosing random first player out of: ");
    Serial.println(g_syncedPeers);
    int randomFirstPlayer;
    randomSeed(*(volatile unsigned long *)0x3FF20E44); // This address has a random value at it.
    for (int i = 0; i < 10; i++)                       // Just to prove it's random for testing
    {
      randomFirstPlayer = (int)random(0, g_syncedPeers); // Pick a random player
      Serial.println(randomFirstPlayer);
    }
    Serial.print("First player: ");
    printMacAddress(g_peers[randomFirstPlayer]);
    copyMacAddress(g_firstPlayer, g_peers[randomFirstPlayer]);
    passTurn(randomFirstPlayer);
  }
  else // Otherwise wait for the lowest MAC to randomize and set first player
  {
    Serial.println("Waiting for first player to be set...");
    printMacAddress(g_firstPlayer);
    Serial.println("");
    g_startSyncTime = millis();
    while (areMacAddressesEqual(g_firstPlayer, DUMMY_ADDRESS))
    {
      yield();
      if (millis() - g_startSyncTime > 20)
      {
        if (millis() - g_startSyncTime > 40)
        {
          digitalWrite(ACTIVITY_LED, LOW);
          g_startSyncTime = millis();
        }
        else
        {
          digitalWrite(ACTIVITY_LED, HIGH);
        }
      }
    }
    Serial.print("Found first player: ");
    printMacAddress(g_firstPlayer);
    copyMacAddress(g_currentPlayer, g_firstPlayer);
  }
  checkIfCurrentPlayer();
}

void initializeFirstPlayer()
{
  Serial.println("sortMacAddressArrayList()");
  sortMacAddressArrayList();
  printPeers(g_peers);
  Serial.println("setFirstPlayer()");
  setFirstPlayer();
}

void playerCountBlink()
{
  unsigned long elapsed = millis() - g_startSyncTime; // Shorthand for the time elapsed since the last blink
  int nextPlayer = 0;                                 // variable to learn the current number of players
  for (int i = 0; i < g_syncedPeers; i++)             // Loop through the peers list up to the max peers
  {
    if (areMacAddressesEqual(g_tempPeers[i], DUMMY_ADDRESS)) // if you find a peer that is empty
    {
      if (elapsed == 2000)
      {
        Serial.print("Found an empty peer: ");
        Serial.println(i);
      }
      nextPlayer = i + 1; // the last registered player is the index before that one. Increase it by one to find the next player count instead of the index, increase it by one more to find the next player
      // Ex: if one player has registered, i will be 1 (index 1 is empty). NextPlayer should be 2, because we're searching for player 2.
      if (nextPlayer < 2) // if there's an error, set the count to 2, because that's the true minimum
      {
        nextPlayer = 2;
      }
      break;
    }
  }
  if (elapsed > 1999) // If it's been longer than 2 second, restart the blink
  {
    if (elapsed == 2000)
    {
      Serial.print("Elapsed: ");
      Serial.println(elapsed);
      Serial.print("nextPlayer: ");
      Serial.println(nextPlayer);
      Serial.print("syncedPeers: ");
      Serial.println(g_syncedPeers);
      printPeers(g_tempPeers);
      Serial.println("");
    }
    g_startSyncTime = millis(); // set the startSyncTime to however long ago we started blinking
  }
  else
  {
    // Blink for 200ms a number of times equal to the current player count
    // Blink for the first 200ms, then turn off for 200ms
    if (elapsed > (unsigned long)(199 + (200 * (nextPlayer + 1)))) // if all blinks have taken place in this period
    {
      digitalWrite(ACTIVITY_LED, LOW); // ensure the led is off
    }
    else
    {
      if (elapsed % 400 < 200) // if we're in the first 200ms of this 40ms period, blink
      {
        digitalWrite(ACTIVITY_LED, HIGH);
      }
      else // otherwise, turn the LED off
      {
        digitalWrite(ACTIVITY_LED, LOW);
      }
    }
  }
}
/*
nextplayer == 3, 1000 == 200 + (200 * (3 + 1))
0   200   400   600   800   1000   1200   1400   1600   1800   2000
On--off   on----off   on----off
*/

void botherFirstPlayer()
{
  autosync_send_struct sending = {0};
  sending.purpose = 5;
  lastSentPacket = sendPacket(sending);
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
  case 3:                         // A new currentPlayer is being set.
    if (receiving.indicator >= 0) // The indicator is the index of the new current player
    {
      copyMacAddress(g_currentPlayer, receiving.address);     // receiving.address is the address of the new current player
      if (areMacAddressesEqual(g_firstPlayer, DUMMY_ADDRESS)) // If the first player has yet to be set,
      {                                                       // then this is the first player
        copyMacAddress(g_firstPlayer, g_currentPlayer);       // so copy the current player to the first player as well
      }
      checkIfCurrentPlayer(); // Turn the LED on if I'm the current player
    }
    break;
  case 4:                                 // A new player has selected their turn order
    registerTurnOrder(receiving.address); // register their turn order and set g_allSelected to 1 if this is the final player
    break;
  case 5:
    if (areMacAddressesEqual(g_currentPlayer, OWN_MAC_ADDRESS))
    {
      g_beingBothered = 1;
    }
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

  // Attach an interrupt to the sync button to detect the need to restart
  attachInterrupt(digitalPinToInterrupt(SYNC_BUTTON), syncInterrupt, CHANGE);
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
    g_syncButtonState = digitalRead(SYNC_BUTTON); // get the physical sync button's state
    g_prevButtonState = digitalRead(PREV_BUTTON);
    g_nextButtonState = digitalRead(NEXT_BUTTON);
    if (g_syncButtonState != 0) // Sync button held down
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
      if (g_syncStarted == 1) // If sync is currently running, run this once
      {
        g_syncStarted = 2; // Sync is ending
        digitalWrite(ACTIVITY_LED, LOW);
        delay(10);                          // Don't crowd the channel
        switchFromBroadcastToPeers();       // Remove the broadcast peer and register the list of peers
        confirmSync();                      // Send a copy of my peer list to my peers
        g_startSyncTime = millis();         // reset the g_startSyncTime
        for (int i = 0; i < MAX_PEERS; i++) // Empty the tempPeers array
        {
          copyMacAddress(g_tempPeers[i], DUMMY_ADDRESS);
        }
        Serial.println("Peer list finally confirmed");

        // Delay 3 seconds to let everyone else catch up
        for (int i = 0; i < 3; i++)
        {
          digitalWrite(NODEMCU_LED, LOW);
          delay(500);
          digitalWrite(NODEMCU_LED, HIGH);
          delay(500);
        }

        initializeFirstPlayer(); // If this device is the lowest MAC, set the first player. Otherwise wait for first player
        delay(50);
        registerTurnOrder(g_firstPlayer); // This device has either set

        g_startSyncTime = millis();
        g_ownPeerListConfirmed = 1; // This is as good as it gets!
        break;
      }
    }
  }

  /********************************************************************************************************************************************
   *                           Player Order Selection
   ********************************************************************************************************************************************/
  // Loop here while waiting for the player order to initialize
  // Blink a number of times equal to the current player number being chosen
  // On any input, if not the first player, send a packet with purpose 4 to register turn order
  // After this device's order is chosen, put LED on solid
  while (1 == 1)
  {
    yield();                                      // This is required in potentially infinite loops
    playerCountBlink();                           // Blink to indicate the current player order being chosen
    g_syncButtonState = digitalRead(SYNC_BUTTON); // get the physical sync button's state
    g_prevButtonState = digitalRead(PREV_BUTTON); // Any button will do
    g_nextButtonState = digitalRead(NEXT_BUTTON);
    if (g_allSelected != 0 || areMacAddressesEqual(g_firstPlayer, OWN_MAC_ADDRESS) || g_syncButtonState != 0 || g_prevButtonState != 0 || g_nextButtonState != 0)
    {
      break; // break out of the above while loop
    }
  }

  /********************************************************************************************************************************************
   *                           Wait for all players to choose their order
   ********************************************************************************************************************************************/
  sendAndRegisterTurnOrder(OWN_MAC_ADDRESS); // Send a packet to put this device in the turn order lineup next
  digitalWrite(ACTIVITY_LED, HIGH);          // Turn the LED on solidly
  while (g_allSelected == 0)                 // Wait here until all are selected
  {
    yield(); // This is required in potentially infinite loops
    g_startSyncTime = millis();
    if (millis() - g_startSyncTime == 1000)
    {
      g_startSyncTime = millis();
      Serial.print("All selected: ");
      Serial.println(g_allSelected);
    }
  }
  copyPeers(g_peers, g_tempPeers); // Copy the new turn order into the global list
  digitalWrite(ACTIVITY_LED, LOW); // Turn off the LED
  Serial.println("All done setting order!");
  delay(1000);
  Serial.println("Current player:");
  printMacAddress(g_currentPlayer);
  Serial.println("");
  checkIfCurrentPlayer();     // Check if we're the current player and turn it back on
  g_newDurationAvailable = 0; // reset this before listening to the potential reset
  g_syncStarted = 0;          // reset this before heading into the next section
  uint8_t botherCount = 0;    // variable for tracking how long we've been bothered
  int botheringStarted = 0;   // variable to indicate if bothering command was received

  /********************************************************************************************************************************************
   *                           Take turns
   ********************************************************************************************************************************************/
  // If we've synced successfully and set player turn order
  while (1 == 1)
  {
    yield();
    g_prevButtonState = digitalRead(PREV_BUTTON);
    g_nextButtonState = digitalRead(NEXT_BUTTON);
    // If the sync button has been held down, see if it was held
    // down long enough to trigger a restart (3 seconds)
    if (g_newDurationAvailable == 1)
    {
      g_newDurationAvailable = 0;
      if (g_duration > 3000 || (g_durationStart != 0 && millis() - g_durationStart > 3000 && millis() - g_durationStart < 3100))
      {
        Serial.println("Restarting:");
        Serial.print("g_duration: ");
        Serial.println(g_duration);
        Serial.print("g_durationStart: ");
        Serial.println(g_durationStart);
        digitalWrite(ACTIVITY_LED, LOW);
        digitalWrite(FLASH_BUTTON, HIGH);
        digitalWrite(NODEMCU_LED, HIGH);
        ESP.restart();
      }
    }

    // If the next button is unpressed, zero its start time
    if (g_nextButtonState == 0)
    {
      g_nextStart = 0;
    }
    // if the previous button is unpressed, zero its start time
    if (g_prevButtonState == 0)
    {
      g_prevStart = 0;
    }
    // if the next button is pressed and its start time is zero, record a new start time
    if (g_nextButtonState != 0 && g_nextStart == 0)
    {
      g_nextStart = millis();
      Serial.print("Next pressed: ");
      Serial.println(g_nextStart);
    }
    // if the previous button is pressed and its start time is zero, record a new start time
    if (g_prevButtonState != 0 && g_prevStart == 0)
    {
      g_prevStart = millis();
      Serial.print("Previous pressed: ");
      Serial.println(g_prevStart);
    }
    // If either the next or previous buttons are held down...
    if (g_nextStart != 0 || g_prevStart != 0)
    {
      // Pass the turn if this device is the current player
      if (areMacAddressesEqual(g_currentPlayer, OWN_MAC_ADDRESS)) // If this device is the current player
      {                                                           // Then pass the turn
        if (g_nextStart != 0)
        {
          passTurn(-1);
        }
        if (g_prevStart != 0)
        {
          passTurn(-2);
        }
      }
      else // if this device wasn't the current player before the button was pressed...
      {
        // If both buttons have been held down for more than 3 seconds,
        // and no more often than every half second,
        // send a bother command to first player
        if (millis() % 500 == 0 && millis() - g_nextStart > 3000 && millis() - g_prevStart > 3000)
        {
          botherFirstPlayer();
        }
      }
    }
    // if this device received a new bother command successfully
    if (g_beingBothered == 1 && botheringStarted == 0)
    {
      // Set a local bothering variable so that we can see if a new bothering command is received later
      botheringStarted = 1;
      g_beingBothered = 0;
    }
    // if bothering has begun
    if (botheringStarted == 1)
    {
      if (millis() - g_startSyncTime > 50)
      {
        digitalWrite(ACTIVITY_LED, LOW); // turn off the LED every 50ms (and the first time)
        if (millis() - g_startSyncTime > 100)
        {
          digitalWrite(ACTIVITY_LED, HIGH); // turn off the LED after 100ms (and the first time)
          g_startSyncTime = millis();       // reset the start time for the LED
          botherCount++;                    // increase the count of times we've blinked
        }
      }
      if (botherCount > 11) // After 11 blinks, check if we're still being bothered
      {
        if (g_beingBothered == 0) // If we've not received a new bother command
        {                         // End the bothering
          botheringStarted = 0;
        }
        else
        {
          g_beingBothered = 0; // Otherwise we will keep being bothered, and reset this to prepare to check yet again
        }
        botherCount = 0; // reset the count
      }
    }

    /*if (millis() - g_startSyncTime > 1000)
    {
      if (g_nextButtonState != 0)
      {
        Serial.println(g_nextButtonState);
        // Serial.println(g_button_pressed);
        Serial.println(millis() - g_startSyncTime);
        // if (g_button_pressed == 0)
        //{
        g_startSyncTime = millis();
        // g_button_pressed = 1;
        passTurn(-1);
        //}
      }
      else
      {
        if (g_prevButtonState != 0)
        {
          Serial.println(g_prevButtonState);
          // Serial.println(g_button_pressed);
          Serial.println(millis() - g_startSyncTime);
          // if (g_button_pressed == 0)
          //{
          g_startSyncTime = millis();
          // g_button_pressed = 1;
          passTurn(-2);
          //}
        }
      }
    }*/
  }
}