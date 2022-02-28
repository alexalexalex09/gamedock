/*

1. Sync button is pressed
2. Send broadcast signal with code for synchronization: "GameDockSync"+MAC
3. Alternate listening and sending (with randomization to make sure everyone gets heard?)
4. When another sync code is received, add it to the syncing list in order of MAC address
5. Display current position in the syncing list on the device
6. Add each MAC address as a peer
7. When the sync button is released, broadcast the full list of MAC addresses
8. Listen for 60 seconds to broadcast messages.
    If the current device's address is in a broadcasted list of addresses, make sure each other address is in the current player list
9. Set the current turn to peer 1
10. If this device is currently taking a turn, send an "I'm taking a turn" ping every second
10a. If not taking a turn, listen for the "I'm taking a turn ping". If not heard for over 5 seconds, advance the current player
11. When the current player presses the "next" or "previous" button, a "SetPlayerTurn"+NextPlayer message is sent to all peers.
12. When a device stops taking a turn, set that device to inactive and skip it.
12a. When receiving a "reactivate"+PlayerNumber message, set that device to active and include it in the rotation
13. If the sync button is held down while a session is active, erase the session

Possible problems:
1) Player 1 runs out of battery. In this case, set player 1 to inactive and switch to player 2 after not hearing player 1 ping for 5 second
2) Player 1 puts a new battery back in after running out of battery.
    In this case, listen for the "I'm taking a turn" ping and set that player to current player. Then send a "reactivate"+PlayerNumber message
    If not found, erase the game and go back to holding mode, the game ended
3) Not every device heard the full list of players. Each device will broadcast a list of players for 60 seconds after the sync button is released to be sure all are included
4) Player 4 runs out of battery. In this case, nothing happens until it's player 4's turn
    When player 3 presses the next button, all devices will set the player turn to 4.
    After 5 seconds, player 4 is not found and set to inactive, and then the current player is advanced











*/