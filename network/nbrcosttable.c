
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"


//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the neighbors'IDs and cost from myNodeID to them
nbr_cost_entry_t * getNbrCostTable()
{	
	int myNodeID;
	int nbrNumber = 0;
	//get my node ID and the # of lines in topology.dat
	if ((myNodeID = topology_getMyNodeID()) < 0) {
		printf("Couldn't get my node ID in getNbrArray.\n");
		return NULL;
	}

  	int numNodes;
	if ((numNodes = topology_getNbrNum()) <= 0) {
		printf("Error getting total number of neighbors from getNbrArray.\n");
		return NULL;
	}
  	nbr_cost_entry_t *nbrCostTable = malloc(sizeof(nbr_cost_entry_t)*numNodes);
  	memset(nbrCostTable, 0, sizeof(nbr_cost_entry_t)* numNodes);
  	int i;
  	for (i = 0; i < numNodes; i++) {
  		nbrCostTable[i].nodeID = -1;
  	}

  	//get topology.dat
	char *fileName = "../topology/topology.dat";
	FILE *pFile = fopen(fileName, "r");
	if (pFile == NULL) {
		perror("Error getting file in getNbrArray.\n");
		return NULL;
	}

	char buffer[200];
	char host1[100];
	char host2[200];
	int h1, h2;
	unsigned int num;
	while (fgets(buffer, sizeof(buffer), pFile)){
		sscanf(buffer, "%s %s %u\n", host1, host2, &num);
		if ((h1 = topology_getNodeIDfromname(host1)) < 0 || (h2 = topology_getNodeIDfromname(host2)) < 0){
			printf("Error. host1 = %s, h1 = %u, host2 = %s, h2 = %u\n", host1, h1, host2, h2);
			return NULL;
		}

		if (h1 == myNodeID)  {
			nbrCostTable[nbrNumber].nodeID = h2;
			nbrCostTable[nbrNumber].cost = topology_getCost(myNodeID, h2);
			nbrNumber++;
		}
		if (h2 == myNodeID) {
			nbrCostTable[nbrNumber].nodeID = h1;
			nbrCostTable[nbrNumber].cost = topology_getCost(myNodeID, h1);
			nbrNumber++;
		}

		memset(buffer, 0, sizeof(char)*200);
		memset(host1, 0, sizeof(char)*100);
		memset(host2, 0, sizeof(char)*100);
	}
	//printf("Returning neighbor cost table with nodes: ");
	//for (i = 0; i < numNodes; i++) {
  	//	printf("(%u with cost %u) ", nbrCostTable[i].nodeID, nbrCostTable[i].cost);
  	//}
	//printf("\n");
	return nbrCostTable;
}

//This function creates a neighbor cost table dynamically 
//and initialize the table with all its neighbors' node IDs and direct link costs.
//The neighbors' node IDs and direct link costs are retrieved from topology.dat file. 
nbr_cost_entry_t* nbrcosttable_create()
{
  return getNbrCostTable();
}

//This function destroys a neighbor cost table. 
//It frees all the dynamically allocated memory for the neighbor cost table.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
  free(nct);
  printf("Successfully destroyed neighbor cost table!\n");
}

//This function is used to get the direct link cost from neighbor.
//The direct link cost is returned if the neighbor is found in the table.
//INFINITE_COST is returned if the node is not found in the table.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
  	int nbrNumber = topology_getNbrNum();
  	for (int i = 0; i < nbrNumber; i++) {
  		if (nct[i].nodeID == nodeID) {
  			return nct[i].cost;
  		}
  	}
  	return INFINITE_COST;
}

//This function prints out the contents of a neighbor cost table.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
	int numNodes = topology_getNbrNum();
	printf("\nNeighbor cost table has nodes: ");
  	for (int i = 0; i < numNodes; i++) {
  		printf("(%u with cost %u) ", nct[i].nodeID, nct[i].cost);
  	}
	printf("\n\n");
}
