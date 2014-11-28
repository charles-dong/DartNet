

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "routingtable.h"

//This is the hash function used the by the routing table
//It takes the hash key - destination node ID as input, 
//and returns the hash value - slot number for this destination node ID.
//
//You can copy makehash() implementation below directly to routingtable.c:
//int makehash(int node) {
//	return node%MAX_ROUTINGTABLE_ENTRIES;
//}
//
int makehash(int node)
{
  return node % MAX_ROUTINGTABLE_SLOTS;
}

//This function creates a routing table dynamically.
//All the entries in the table are initialized to NULL pointers.
//Then for all the neighbors with a direct link, create a routing entry using the neighbor itself as the 
//next hop node, and insert this routing entry into the routing table. 
//The dynamically created routing table structure is returned.
routingtable_t* routingtable_create()
{
	routingtable_t *routingTable = malloc(sizeof(routingtable_entry_t *)*MAX_ROUTINGTABLE_SLOTS);
	memset(routingTable, 0, sizeof(routingtable_entry_t *)*MAX_ROUTINGTABLE_SLOTS);

	//initialize all hash entries to NULL
	for (int i = 0; i < MAX_ROUTINGTABLE_SLOTS; i++) {
		routingTable->hash[i] = NULL;
	}
 	
 	//initialize neighbors
	int* nbrArray = topology_getNbrArray();
	int numNbrs = topology_getNbrNum();
	for (int j = 0; j < numNbrs; j++){
		routingtable_entry_t *temp = malloc(sizeof(routingtable_entry_t));
		temp->destNodeID = nbrArray[j];
		temp->nextNodeID = nbrArray[j];
		temp->next = NULL;
		int tempHash = makehash(nbrArray[j]);
		if (routingTable->hash[tempHash] == NULL) {
			routingTable->hash[tempHash] = temp;
		} else {
			routingtable_entry_t *tempEntry = routingTable->hash[tempHash];
			while (tempEntry->next != NULL) {
				tempEntry = tempEntry->next;
			}
			tempEntry->next = temp;
		}
	}

	//routingtable_print(routingTable);
	free(nbrArray);
 	return routingTable;
}

//This funtion destroys a routing table. 
//All dynamically allocated data structures for this routing table are freed.
void routingtable_destroy(routingtable_t* routingtable)
{
	for (int i = 0; i < MAX_ROUTINGTABLE_SLOTS; i++) {
		routingtable_entry_t *currentEntry = routingtable->hash[i];
  		while (currentEntry != NULL) {
  			routingtable_entry_t *tempEntry = currentEntry;
  			currentEntry = currentEntry->next;
  			free(tempEntry);
  		}
	}
	free(routingtable);
	printf("Routing Table successfully destroyed!\n");
}

//This function updates the routing table using the given destination node ID and next hop's node ID.
//If the routing entry for the given destination already exists, update the existing routing entry.
//If the routing entry of the given destination is not there, add one with the given next node ID.
//Each slot in routing table contains a linked list of routing entries due to conflicting hash keys (differnt hash keys (destination node ID here) may have same hash values (slot entry number here)).
//To add an routing entry to the hash table:
//First use the hash function makehash() to get the slot number in which this routing entry should be stored. 
//Then append the routing entry to the linked list in that slot.
void routingtable_setnextnode(routingtable_t* routingtable, int destNodeID, int nextNodeID)
{
	int hashNum = makehash(destNodeID);
	if (routingtable->hash[hashNum]) { //if there's something at the hash
		routingtable_entry_t *currentEntry = routingtable->hash[hashNum];
		if (currentEntry->destNodeID == destNodeID){ //if it's the entry we want..
			currentEntry->nextNodeID = nextNodeID;
			//printf("Set entry with destID %d to nextID %d.\n", currentEntry->destNodeID, currentEntry->nextNodeID);
			return;	
		} 
		while (currentEntry->next != NULL) { //else look through linked list for entry we want
			currentEntry = currentEntry->next;
			if (currentEntry->destNodeID == destNodeID){
				currentEntry->nextNodeID = nextNodeID;
				//printf("Set entry with destID %d to nextID %d.\n", currentEntry->destNodeID, currentEntry->nextNodeID);
				return;	
			} 
		} 
		//if we couldn't find the entry we wanted at the hash, create it
		currentEntry->next = malloc(sizeof(routingtable_entry_t));
		currentEntry->next->destNodeID = destNodeID;
		currentEntry->next->nextNodeID = nextNodeID;
		currentEntry->next->next = NULL;
		//printf("Created entry with destID %d and nextID %d.\n", currentEntry->next->destNodeID, currentEntry->next->nextNodeID);
		return;
	} else { //if nothing at the hash.. create it
		routingtable->hash[hashNum] = malloc(sizeof(routingtable_entry_t));
		routingtable->hash[hashNum]->destNodeID = destNodeID;
		routingtable->hash[hashNum]->nextNodeID = nextNodeID;
		routingtable->hash[hashNum]->next = NULL;
		//printf("Created entry with destID %d and nextID %d.\n", routingtable->hash[hashNum]->destNodeID, routingtable->hash[hashNum]->nextNodeID);
		return;
	}
}

//This function looks up the destNodeID in the routing table.
//Since routing table is a hash table, this opeartion has O(1) time complexity.
//To find a routing entry for a destination node, you should first use the hash function makehash() to get
// the slot number and then go through the linked list in that slot to search the routing entry.
//If the destNodeID is found, return the nextNodeID for this destination node.
//If the destNodeID is not found, return -1.
int routingtable_getnextnode(routingtable_t* routingtable, int destNodeID)
{
	int hashNum = makehash(destNodeID);
	if (routingtable->hash[hashNum]) {
		routingtable_entry_t *currentEntry = routingtable->hash[hashNum];
		if (currentEntry->destNodeID == destNodeID){ //if it's the entry we want..
			return currentEntry->nextNodeID;
		}
		while (currentEntry->next != NULL) { //else look through linked list for entry we want
			currentEntry = currentEntry->next;
			if (currentEntry->destNodeID == destNodeID){
				return currentEntry->nextNodeID;
			}
		}
		return -1;
	} else {
		return -1;
	}
}

//This function prints out the contents of the routing table
void routingtable_print(routingtable_t* routingtable)
{
  printf("\nRouting table has nodes: ");
  	for (int i = 0; i < MAX_ROUTINGTABLE_SLOTS; i++) {
  		printf("\nhash[%d]: ", i);
  		routingtable_entry_t *tempEntry = routingtable->hash[i];
  		while (tempEntry != NULL) {
  			printf("(dest %d with nextNode %d) ", tempEntry->destNodeID, tempEntry->nextNodeID);
  			tempEntry = tempEntry->next;
  		}
  	}
	printf("\n\n");
}
