
/**
 * @brief variable to hold a dummy address (all zeroes)
 *
 */
uint8_t DUMMY_ADDRESS[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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
 * @param addressesToPrint an array of mac addresses
 * @param maxPeers the maximum number of addresses that could theoretically be in this list
 *
 */
void printMacAddresses(uint8_t addressesToPrint[][6])
{
    Serial.println("Printing Peers:");
    for (unsigned int i = 0; i < sizeof(addressesToPrint) / sizeof(addressesToPrint[0]); i++)
    {
        if (!areMacAddressesEqual(addressesToPrint[i], DUMMY_ADDRESS))
        {
            Serial.print(i + 1);
            Serial.print(": ");
            printMacAddress(addressesToPrint[i]);
            Serial.println();
        }
        else
        {
            break;
        }
    }
}

/**
 * @brief push a new peer onto an array
 * Depends on areMacAddressesEqual
 *
 * Iterates through the given array to push a new peer onto the first slot that is all zeroes
 * @param peerList the array of mac address to push to
 * @param newAddress the new address to add
 *
 */
void pushNewMacAddress(uint8_t peerList[][6], uint8_t newAddress[6])
{
    for (int i = 0; i < sizeof(peerList) / sizeof(peerList[0]); i++)
    {
        if (areMacAddressesEqual(peerList[i], DUMMY_ADDRESS))
        {
            Serial.println("Copying...");
            for (int j = 0; j < 6; j++)
            {
                peerList[i][j] = newAddress[j];
                Serial.print(newAddress[j]);
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
void removeAddressFromArray(uint8_t peerList[][6], int peerNumber)
{
    int maxAddresses = sizeof(peerList) / sizeof(peerList[0]);
    if (peerNumber > maxAddresses - 1)
    {
        Serial.print("****WARNING! PEER NOT REMOVED! PARAMETER TOO HIGH: ");
        Serial.println(peerNumber);
    }
    else
    {
        // Remove the identified peer
        for (int i = 0; i < 6; i++)
        {
            peerList[peerNumber][i] = 0x00;
        }
        // Look at the next peer
        peerNumber++;
        // Iterate through the remaining peers (if it was 19, it won't interate)
        for (int i = peerNumber; i < maxAddresses; i++)
        {
            // If the current address isn't 0
            if (!areMacAddressesEqual(peerList[i], DUMMY_ADDRESS))
            {
                // copy the current address into the previous (empty) address
                for (int j = 0; j < 6; j++)
                {
                    peerList[i - 1][j] = peerList[i][j];
                }
                // empty the current address
                for (int j = 0; j < 6; j++)
                {
                    peerList[i + 1][j] = 0x00;
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

/**
 * @brief returns the number of addresses in a list that aren't blank
 * @param macAddressArray the array of mac addresses to look through
 *
 */
int totalUsedAddressesInArray(uint8_t macAddressArray[][6])
{
    int syncedPeers = 0;
    for (int i = 0; i < sizeof(macAddressArray) / sizeof(macAddressArray[0]); i++)
    {
        if (areMacAddressesEqual(macAddressArray[i], DUMMY_ADDRESS))
        {
            syncedPeers = i;
            return i;
        }
    }
    return syncedPeers;
}

/**
 * @brief sorts an array of Mac addresses
 *
 * @param macAddressArray the array of addresses to sort
 * @param totalAddresses the number of addresses in that array
 */
void sortMacAddressArrayList(uint8_t macAddressArray[][6])
{
    qsort(macAddressArray, totalUsedAddressesInArray(macAddressArray), sizeof(macAddressArray[0]), macAddressSorter);
}
