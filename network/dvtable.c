
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//This function creates a dvtable(distance vector table) dynamically.
//A distance vector table contains the n+1 entries, where n is the number of the neighbors of this node,
// and the other one is for this node itself. 
//Each entry in distance vector table is a dv_t structure which contains a source node ID and an array
// of N dv_entry_t structures where N is the number of all the nodes in the overlay.
//Each dv_entry_t contains a destination node address the the cost from the source node to this destination node.
//The dvtable is initialized in this function.
//The link costs from this node to its neighbors are initialized using direct link cost retrived from topology.dat. 
//Other link costs are initialized to INFINITE_COST.
//The dynamically created dvtable is returned.
dv_t* dvtable_create()
{
	  int myNodeID = topology_getMyNodeID();
	  int nbrNumber = topology_getNbrNum();
  	int numEntries = nbrNumber + 1;
  	int numTotalNodes = topology_getNodeNum();
  	int* nbrArray = topology_getNbrArray();
  	int* nodeArray = topology_getNodeArray();

  	dv_t *dvTable = malloc(sizeof(dv_t)*numEntries);
  	memset(dvTable, 0, sizeof(dv_t)*numEntries);

  	//initialize own node
  	dvTable[0].nodeID = myNodeID;
  	dvTable[0].dvEntry = malloc(sizeof(dv_entry_t)*numTotalNodes);
  	for (int i = 0; i < numTotalNodes; i++) {
  		dvTable[0].dvEntry[i].nodeID = nodeArray[i];
  		dvTable[0].dvEntry[i].cost = topology_getCost(dvTable[0].nodeID, nodeArray[i]);
  	}

  	//initialize neighbors

  	for (int j = 1; j < numEntries; j++) {
  		dvTable[j].nodeID = nbrArray[j-1];
	  	dvTable[j].dvEntry = malloc(sizeof(dv_entry_t)*numTotalNodes);
	  	for (int i = 0; i < numTotalNodes; i++) {
	  		dvTable[j].dvEntry[i].nodeID = nodeArray[i];
	  		dvTable[j].dvEntry[i].cost = INFINITE_COST;//topology_getCost(dvTable[j].nodeID, nodeArray[i]);
	  	}
  	}
  	
  	//print
  	//dvtable_print(dvTable);

  	free(nodeArray);
  	free(nbrArray);
  	return dvTable;
}

//This function destroys a dvtable. 
//It frees all the dynamically allocated memory for the dvtable.
void dvtable_destroy(dv_t* dvTable)
{
    int nbrNumber = topology_getNbrNum();
    int numEntries = nbrNumber + 1;
    for (int i = 0; i < numEntries; i++) {
    	   free(dvTable[i].dvEntry);
    }
    free(dvTable);
    printf("Successfully destroyed distance vector table!\n");
}

//This function sets the link cost between two nodes in dvtable.
//If those two nodes are found in the table and the link cost is set, return 1.
//Otherwise, return -1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
    int nbrNumber = topology_getNbrNum();
    int numEntries = nbrNumber + 1;
    int numTotalNodes = topology_getNodeNum();
    int i = 0;

    do {
        if (dvtable[i].nodeID == fromNodeID) { //found the right source node's array of dv entries
            int j = 0;
            do {
                if (dvtable[i].dvEntry[j].nodeID == toNodeID) { //found right dv entry
                    dvtable[i].dvEntry[j].cost = cost;
                    return 1;
                }
                j++;
            } while (j < numTotalNodes);
            printf("Found right source node array of dv entries, but not the dest node.\n");
        }

        i++;
    } while (i < numEntries);

    return -1;
}

//This function returns the link cost between two nodes in dvtable
//If those two nodes are found in dvtable, return the link cost. 
//otherwise, return INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{ 
    int nbrNumber = topology_getNbrNum();
    int numEntries = nbrNumber + 1;
    int numTotalNodes = topology_getNodeNum();
    int i = 0;

        do {
        if (dvtable[i].nodeID == fromNodeID) { //found the right source node's array of dv entries
            int j = 0;
            do {
                if (dvtable[i].dvEntry[j].nodeID == toNodeID) { //found right dv entry
                    return dvtable[i].dvEntry[j].cost;
                }
                j++;
            } while (j < numTotalNodes);
            printf("Found right source node array of dv entries, but not the dest node.\n");
        }

        i++;
    } while (i < numEntries);

    printf("Couldn't find cost from %d to %d. Returning INFINITE_COST.\n", fromNodeID, toNodeID);
    return INFINITE_COST;
}

//This function prints out the contents of a dvtable.
void dvtable_print(dv_t* dvtable)
{
    int nbrNumber = topology_getNbrNum();
    int numEntries = nbrNumber + 1;
    int numTotalNodes = topology_getNodeNum();
    printf("\nPrinting distance vector table:\n");
    for (int j = 0; j < numEntries; j++) {
      printf("From source node %d...\n", dvtable[j].nodeID);
      for (int i = 0; i < numTotalNodes; i++) {
        printf("\tto dest node %d has cost %u.\n",dvtable[j].dvEntry[i].nodeID, dvtable[j].dvEntry[i].cost);
      }
    }
}


//returns dv_t for specified source node ID (used in route update packets)
dv_t *getRouteUpdateData(dv_t* dvtable, int src_nodeID){
    int nbrNumber = topology_getNbrNum();
    int numEntries = nbrNumber + 1;
    int i = 0;

    do {
        if (dvtable[i].nodeID == src_nodeID)  //found the right source node's array of dv entries
          return &dvtable[i];
        i++;
    } while (i < numEntries);

    printf("Couldn't find my dv table in getRouteUpdateData (for src_nodeID %d)!\n", src_nodeID);
    dvtable_print(dvtable);
    return NULL;
}


