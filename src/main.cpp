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
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <stdbool.h>

void beginSync();
void continueSync();
void endSync();
void testBroadcast();
void doSomething();
void doNothing();

/**
 * @brief Used for testBroadcast() to determine if it's been enough time to test again.
 *
 */
unsigned long lastTest = 0;

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
uint8_t BROADCAST_PEER[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/**
 * @brief MAC address
 *
 * Address of the device
 */
uint8_t MACADDRESS[6];

/**
 * @brief Custom struct for communication between devices
 *
 * @param MAC contains the sending device mac address
 * @param syncing is a bool, needs #include <stdbool.h>
 */
typedef struct sync_struct
{
  uint8_t MAC[6];
  bool syncing;
} sync_struct;

/**
 * @brief Callback for when data is sent
 * https://randomnerdtutorials.com/esp-now-one-to-many-esp8266-nodemcu/
 * @param mac_addr destination address
 * @param sendStatus success or failure, 0 is success
 */
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus)
{
  digitalWrite(ACTIVITY_LED, HIGH);
  char macStr[18];
  Serial.print("Packet to:");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
  Serial.print(" send status: ");
  if (sendStatus == 0)
  {
    Serial.println("Delivery success");
  }
  else
  {
    Serial.println("Delivery fail");
  }
  delay(1000);
  digitalWrite(ACTIVITY_LED, LOW);
}

/**
 * @brief Callback for when data is received (testBroadcast)
 * https://randomnerdtutorials.com/esp-now-one-to-many-esp8266-nodemcu/
 * @param mac sender mac address
 * @param incomingData data received
 * @param len length of bytes received
 */
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  digitalWrite(ACTIVITY_LED, HIGH);
  uint8_t myData;
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("data: ");
  Serial.println(myData);
  Serial.println();
  delay(100);
  digitalWrite(ACTIVITY_LED, LOW);
}

void setup()
{
  // Set appropriate bit rate
  Serial.begin(115200);
  // Not sure why wifi needs to be in STA mode yet
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  Serial.println();
  Serial.print("ESP8266 Board MAC Address:  ");
  Serial.println(WiFi.macAddress());
  WiFi.macAddress(MACADDRESS);

  // Initialize ESPNow
  if (esp_now_init() != 0)
  {
    Serial.println("Problem during ESP-NOW init");
    return;
  }

  // Set up peers
  // https://techtutorialsx.com/2019/10/20/esp32-getting-started-with-esp-now/
  Serial.println("Adding peer...");
  Serial.println(esp_now_add_peer(BROADCAST_PEER, ESP_NOW_ROLE_SLAVE, WIFI_CHANNEL, NULL, 0));
  Serial.println("Registering send:");
  Serial.println(esp_now_register_send_cb(OnDataSent));
  Serial.println("Registering receive:");
  Serial.println(esp_now_register_recv_cb(OnDataRecv));
  // Set pins
  pinMode(SYNC_BUTTON, INPUT);
  pinMode(BUILTINLED, OUTPUT);
  digitalWrite(BUILTINLED, HIGH);
  pinMode(NODEMCU_LED, OUTPUT);
  digitalWrite(NODEMCU_LED, HIGH);
  pinMode(ACTIVITY_LED, OUTPUT);
}

void loop()
{
  syncButtonState = digitalRead(SYNC_BUTTON);
  if (syncButtonState != 0) // Sync button is held down
  {
    digitalWrite(BUILTINLED, LOW);
    digitalWrite(ACTIVITY_LED, HIGH);
    if (syncing == false) // Not already syncing
    {
      beginSync();
      Serial.println("Button pressed");
      Serial.println(syncButtonState);
    }
    else // already syncing
    {
      continueSync();
    }
  }
  else // Sync button is not held down
  {
    digitalWrite(BUILTINLED, HIGH);
    digitalWrite(ACTIVITY_LED, LOW);
    if (syncing == true) // Syncing has started
    {
      endSync();
      Serial.println("Button released");
      Serial.println(syncButtonState);
    }
    else
    {
      doNothing(); // No syncing and no button press
    }
  }

  if (millis() - lastTest > 5000)
  {
    testBroadcast();
    lastTest = millis();
  }
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
  memcpy(gameDockSync.MAC, MACADDRESS, 6);
  for (int i = 0; i < 6; i++)
  {
    Serial.print(gameDockSync.MAC[i]);
  }
  Serial.println(' ');
  esp_now_send(BROADCAST_PEER, (uint8_t *)&gameDockSync, sizeof(sync_struct));
}

void testBroadcast()
{
  Serial.println("Testing...");
  digitalWrite(NODEMCU_LED, LOW);
  int x = millis();
  Serial.println(esp_now_send(BROADCAST_PEER, (uint8_t *)&x, sizeof(int)));
  digitalWrite(NODEMCU_LED, HIGH);
  Serial.println("Test complete");
}

void continueSync()
{
}

void doNothing()
{
}

void endSync()
{
  syncing = false;
}