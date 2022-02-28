# Welcome to GameDock

In Tabletop Simulator, the **Turns** feature shows you whose turn it is and what the overall turn order is. It pings you when it's your turn, which is really helpful when a game has long turns and you went to get a drink or zoned out. The GameDock brings that feature into reality, along with some extra features.

## Project Goals

The end result will be:
1. Small enough to fit in a purse, bag, or maybe even a pocket
2. Customizable 
3. Simple to use
4. Easily extensible with attachments that could be 3D printed

Functions:
1. Displays the user's player color
2. Shows whose turn it is
3. Displays whose turn is next
4. Displays whose turn is previous
5. Displays which player number the current player is
6. Easy to sync with other people playing the same game
7. Can be used alone to simply show turn order or used with other units to notify individual players when it's their turn

Features:
1. Players can choose their own color
2. Players can choose their turn order (player number)
3. Play order can be randomized
4. Current player can send the turn to the next or previous player
5. Player can see their own number, own color, and color of the next and previous player
6. Player's color can be seen by others at the table
7. Accessorizable with 3D-printable attachments such as dice towers, token holders, etc.

Method:
- Communication using ESP32 boards with the ESPNow protocol
- RGB LEDs to show player colors
- Synchronization by having all players simultaneously press a sync button and release after all have pressed
