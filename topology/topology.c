//FILE: topology/topology.c
//
//Description: this file implements some helper functions used to parse 
//the topology file 
//
//Date: May 3,2010

#include "topology.h"
#include "../common/constants.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

//this function returns node ID of the given hostname
//the node ID is an integer of the last 8 digit of the node's IP address
//for example, a node with IP address 202.120.92.3 will have node ID 3
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromname(char* hostname) 
{
	struct hostent *he;
    struct in_addr **addr_list;

    if ((he = gethostbyname(hostname)) == NULL) {  // get the host info
        perror("gethostbyname failed.\n");
        return -1;
    }
    addr_list = (struct in_addr **)he->h_addr_list;

    //printf("Hostname: %s IP: %s\n", hostname, inet_ntoa(*addr_list[0]));

    //get nodeID from IP
    return topology_getNodeIDfromip(addr_list[0]);
 
}

//this function returns node ID from the given IP address
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromip(struct in_addr* addr)
{
  
    int a, b, c, d;
    if (sscanf(inet_ntoa(*addr), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    	return -1;
    } else  {
    	return d;
    }
}

//this function returns my node ID
//if my node ID can't be retrieved, return -1
int topology_getMyNodeID()
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
    //printf("My hostname is %s.\n", hostname);
    return topology_getNodeIDfromname(hostname);
}

//this functions parses the topology information stored in topology.dat
//returns the number of neighbors
int topology_getNbrNum()
{
	int myNodeID;
	int nbrNumber = 0;

	//get my node ID and topology.dat
	if ((myNodeID = topology_getMyNodeID()) < 0)
		return -1;
	char *fileName = "../topology/topology.dat";
	FILE *pFile = fopen(fileName, "r");
	if (pFile == NULL) {
		perror("Error getting file in get NbrNum.\n");
		return -1;
	}

	char buffer[200];
	char host1[100];
	char host2[200];
	int num, h1, h2;
	while (fgets(buffer, sizeof(buffer), pFile)){
		sscanf(buffer, "%s %s %d\n", host1, host2, &num);
		if ((h1 = topology_getNodeIDfromname(host1)) < 0 || (h2 = topology_getNodeIDfromname(host2)) < 0){
			printf("Error. host1 = %s, h1 = %d, host2 = %s, h2 = %d\n", host1, h1, host2, h2);
			return -1;
		}
		if (h1 == myNodeID || h2 == myNodeID)
			nbrNumber++;
		memset(buffer, 0, sizeof(char)*200);
		memset(host1, 0, sizeof(char)*100);
		memset(host2, 0, sizeof(char)*100);
	}
	fclose(pFile);
	return nbrNumber;
}


//returns neighbor names as dynamically allocated char** array entries
int getNeighborNames(char **nbrNames, int numNeighbors){
	int myNodeID;
	int nbrNumber = 0;
	//get my node ID and the # of lines in topology.dat
	if ((myNodeID = topology_getMyNodeID()) < 0)
		return -1;

	char *fileName = "../topology/topology.dat";
	FILE *pFile = fopen(fileName, "r");
	if (pFile == NULL) {
		perror("Error getting file in getNeighborNames.\n");
		return -1;
	}

	char buffer[200];
	char host1[100];
	char host2[200];
	int num, h1, h2;
	while (fgets(buffer, sizeof(buffer), pFile)){
		sscanf(buffer, "%s %s %d\n", host1, host2, &num);
		if ((h1 = topology_getNodeIDfromname(host1)) < 0 || (h2 = topology_getNodeIDfromname(host2)) < 0){
			printf("Error. host1 = %s, h1 = %d, host2 = %s, h2 = %d\n", host1, h1, host2, h2);
			return -1;
		}
		if (h1 == myNodeID ) { //then host2 is neighbor
			nbrNames[nbrNumber] = malloc(sizeof(char)*100);
			memset(nbrNames[nbrNumber], 0, sizeof(char)*100);
			memcpy(nbrNames[nbrNumber], host2, 100);
			nbrNumber++;
		} else if (h2 == myNodeID) { //then host1 is neighbor
			nbrNames[nbrNumber] = malloc(sizeof(char)*100);
			memset(nbrNames[nbrNumber], 0, sizeof(char)*100);
			memcpy(nbrNames[nbrNumber], host1, 100);
			nbrNumber++;
		}
			
		memset(buffer, 0, sizeof(char)*200);
		memset(host1, 0, sizeof(char)*100);
		memset(host2, 0, sizeof(char)*100);
	}
	fclose(pFile);
	if (nbrNumber == numNeighbors)
		return 1;
	else
		return -1;
}


//this functions parses the topology information stored in topology.dat
//returns the cost of the direct link between the two given nodes 
//if no direct link between the two given nodes, INFINITE_COST is returned
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{	
	if (fromNodeID == toNodeID) {
		return 0;
	}
	
	//get topology.dat
	char *fileName = "../topology/topology.dat";
	FILE *pFile = fopen(fileName, "r");
	if (pFile == NULL) {
		perror("Error getting file in getCost.\n");
		return -1;
	}

	char buffer[200];
	char host1[100];
	char host2[200];
	int h1, h2;
	unsigned int num;
	while (fgets(buffer, sizeof(buffer), pFile)){
		sscanf(buffer, "%s %s %u\n", host1, host2, &num);
		if ((h1 = topology_getNodeIDfromname(host1)) < 0 || (h2 = topology_getNodeIDfromname(host2)) < 0){
			printf("Error. host1 = %s, h1 = %d, host2 = %s, h2 = %d\n", host1, h1, host2, h2);
			return -1;
		}
		if ((h1 == fromNodeID && h2 == toNodeID) || (h2 == fromNodeID && h1 == toNodeID)) {
			//printf("Cost between %d and %d is %u.\n", h1, h2, num);
			fclose(pFile);
			return num;
		}
		memset(buffer, 0, sizeof(char)*200);
		memset(host1, 0, sizeof(char)*100);
		memset(host2, 0, sizeof(char)*100);
	}
	fclose(pFile);
  	return INFINITE_COST;
}

//this functions parses the topology information stored in topology.dat
//returns the number of total nodes in the overlay 
int topology_getNodeNum()
{ 
  	int nodeIDs[MAX_NODE_NUM];
  	memset(nodeIDs, 0, sizeof(int)* MAX_NODE_NUM);
  	for (int i = 0; i < MAX_NODE_NUM; i++) {
  		nodeIDs[i] = -1;
  	}

  	//get topology.dat
	char *fileName = "../topology/topology.dat";
	FILE *pFile = fopen(fileName, "r");
	if (pFile == NULL) {
		perror("Error getting file in getNodeNum.\n");
		return -1;
	}

	char buffer[200];
	char host1[100];
	char host2[200];
	int h1, h2;
	unsigned int num;
	while (fgets(buffer, sizeof(buffer), pFile)){
		sscanf(buffer, "%s %s %u\n", host1, host2, &num);
		if ((h1 = topology_getNodeIDfromname(host1)) < 0 || (h2 = topology_getNodeIDfromname(host2)) < 0){
			printf("Error. host1 = %s, h1 = %d, host2 = %s, h2 = %d\n", host1, h1, host2, h2);
			return -1;
		}

		int j = 0;
		while (nodeIDs[j] != -1) {
			if (h1 == nodeIDs[j]) {
				j = -1;
				break;
			}
			j++;
		}
		if (j != -1 && j < MAX_NODE_NUM) {
			nodeIDs[j] = h1;
		}
		j = 0;
		while (nodeIDs[j] != -1) {
			if (h2 == nodeIDs[j]) {
				j = -1;
				break;
			}
			j++;
		}
		if (j != -1 && j < MAX_NODE_NUM) {
			nodeIDs[j] = h2;
		}


		memset(buffer, 0, sizeof(char)*200);
		memset(host1, 0, sizeof(char)*100);
		memset(host2, 0, sizeof(char)*100);
	}

	int count = 0;
	while (nodeIDs[count] != -1) {
		count++;
	}
	fclose(pFile);
	return count;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the nodes' IDs in the overlay network  
int* topology_getNodeArray()
{
	int numNodes;
	if ((numNodes = topology_getNodeNum()) <= 0) {
		printf("Error getting total number of nodes from getNodeArray.\n");
		return NULL;
	}
  	int *nodeIDs = malloc(sizeof(int)*numNodes);
  	memset(nodeIDs, 0, sizeof(int)* numNodes);
  	int i;
  	for (i = 0; i < numNodes; i++) {
  		nodeIDs[i] = -1;
  	}

  	//get topology.dat
	char *fileName = "../topology/topology.dat";
	FILE *pFile = fopen(fileName, "r");
	if (pFile == NULL) {
		perror("Error getting file in getNodeArray.\n");
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
			printf("Error. host1 = %s, h1 = %d, host2 = %s, h2 = %d\n", host1, h1, host2, h2);
			return NULL;
		}

		int j = 0;
		while (nodeIDs[j] != -1) {
			if (h1 == nodeIDs[j]) {
				j = -1;
				break;
			}
			j++;
		}
		if (j != -1 && j < MAX_NODE_NUM) {
			nodeIDs[j] = h1;
		}
		j = 0;
		while (nodeIDs[j] != -1) {
			if (h2 == nodeIDs[j]) {
				j = -1;
				break;
			}
			j++;
		}
		if (j != -1 && j < MAX_NODE_NUM) {
			nodeIDs[j] = h2;
		}


		memset(buffer, 0, sizeof(char)*200);
		memset(host1, 0, sizeof(char)*100);
		memset(host2, 0, sizeof(char)*100);
	}
	//printf("Topology.c returning node array with nodes: ");
	//for (i = 0; i < numNodes; i++) {
  	//	printf("%d ", nodeIDs[i]);
  	//}
	//printf("\n");
	fclose(pFile);
	return nodeIDs;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the neighbors'IDs  
int* topology_getNbrArray()
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
  	int *nodeIDs = malloc(sizeof(int)*numNodes);
  	memset(nodeIDs, 0, sizeof(int)* numNodes);
  	int i;
  	for (i = 0; i < numNodes; i++) {
  		nodeIDs[i] = -1;
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
			printf("Error. host1 = %s, h1 = %d, host2 = %s, h2 = %d\n", host1, h1, host2, h2);
			return NULL;
		}

		if (h1 == myNodeID)  {
			nodeIDs[nbrNumber] = h2;
			nbrNumber++;
		}
		if (h2 == myNodeID) {
			nodeIDs[nbrNumber] = h1;
			nbrNumber++;
		}

		memset(buffer, 0, sizeof(char)*200);
		memset(host1, 0, sizeof(char)*100);
		memset(host2, 0, sizeof(char)*100);
	}
	/*printf("Returning neighbor node array with nodes: ");
	for (i = 0; i < numNodes; i++) {
  		printf("%d ", nodeIDs[i]);
  	}
	printf("\n");*/
	fclose(pFile);
	return nodeIDs;
}

//return IP
struct in_addr getMyIP() {
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	struct hostent *he;
    struct in_addr **addr_list;

    if ((he = gethostbyname(hostname)) == NULL) {  // get the host info
        perror("gethostbyname failed.\n");
    }
    addr_list = (struct in_addr **)he->h_addr_list;
    return *addr_list[0];
}

