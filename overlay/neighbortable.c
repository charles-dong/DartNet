//FILE: overlay/neighbortable.c
//
//Description: this file the API for the neighbor table
//
//Date: May 03, 2010

#include "neighbortable.h"
#include "../topology/topology.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

//This function first creates a neighbor table dynamically. It then parses the topology/topology.dat file
// and fill the nodeID and nodeIP fields in all the entries, initialize conn field as -1 .
//return the created neighbor table
nbr_entry_t* nt_create()
{	
	//get number of neighbors
	int nbrNumber;
	if ((nbrNumber = topology_getNbrNum()) < 0){
		perror("Couldn't get neighbor number.\n");
		return NULL;
	}
	//malloc neighbor table
  	nbr_entry_t *nbrTable = malloc(sizeof(nbr_entry_t)*nbrNumber);
  	memset(nbrTable, 0, sizeof(nbr_entry_t)*nbrNumber);

  	//get names of neighbors
  	char **nbrNames = malloc(sizeof(char *)* nbrNumber);
  	memset(nbrNames, 0, sizeof(char *)*nbrNumber);
  	if (getNeighborNames(nbrNames, nbrNumber) < 0) {
  		printf("Error getting neighbor names.\n");
  		return NULL;
  	}

  	//populate neighbor table
  	for (int i = 0; i < nbrNumber; i++){

  		//get node ID from hostname
  		if ((nbrTable[i].nodeID = topology_getNodeIDfromname(nbrNames[i])) < 0) {
  			perror("Couldn't get nodeID from name whilst populating neighbor table.\n");
  			return NULL;
  		}

  		//get IP from hostname
  		struct hostent *he;
  		if ((he = gethostbyname(nbrNames[i])) == NULL) {
  			perror("Couldn't get IP from name whilst populating neighbor table.\n");
  			return NULL;
  		}
  		nbrTable[i].nodeIP = *(in_addr_t *)he->h_addr_list[0];

  		//conn initialized as -1
  		nbrTable[i].conn = -1;

  		free(nbrNames[i]);
  	}

  	free(nbrNames);
  	return nbrTable;
}

//This function destroys a neighbortable. It closes all the connections and frees all the dynamically
// allocated memory.
void nt_destroy(nbr_entry_t* nt)
{
  int i;
  int nbrNum = topology_getNbrNum();
  for (i = 0; i < nbrNum; i++) {
    if (nt[i].conn != -1) {
      close(nt[i].conn);
      printf("Closed connection to neighbor with nodeID %d (conn %d).\n", nt[i].nodeID, nt[i].conn);
    }
  }
  free(nt);
  printf("Successfully destroyed neighbor table!\n");
}




//unused

//This function is used to assign a TCP connection to a neighbor table entry for a neighboring node.
// If the TCP connection is successfully assigned, return 1, otherwise return -1
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
  return 0;
}
