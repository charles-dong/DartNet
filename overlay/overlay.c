//FILE: overlay/overlay.c
//
//Description: this file implements a ON process 
//A ON process first connects to all the neighbors and then starts listen_to_neighbor threads each of which keeps receiving the incoming packets from a neighbor and forwarding the received packets to the SNP process. Then ON process waits for the connection from SNP process. After a SNP process is connected, the ON process keeps receiving sendpkt_arg_t structures from the SNP process and sending the received packets out to the overlay network. 
//
//Date: April 28,2008


#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>
#include <errno.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "overlay.h"
#include "../topology/topology.h"
#include "neighbortable.h"

//you should start the ON processes on all the overlay hosts within this period of time
#define OVERLAY_START_DELAY 20 

/**************************************************************/
//declare global variables
/**************************************************************/

//declare the neighbor table as global variable 
nbr_entry_t* nt; 
//declare the TCP connection to SNP process as global variable
int network_conn; 
int initialConnectToSNP;


/**************************************************************/
//implementation overlay functions
/**************************************************************/

// This thread opens a TCP port on CONNECTION_PORT and waits for the incoming connection from all the neighbors that have a larger node ID than my nodeID,
// After all the incoming connections are established, this thread terminates 
void* waitNbrs(void* arg) {

	int myNodeID, nbrNum, i;
	int nbrsWithLargerIDs = 0;

	//make sure there's a neighbor table
	if (!nt) {
		nt = nt_create();
	}
	//get my node ID
	if ((myNodeID = topology_getMyNodeID()) < 0) {
		printf("Error getting my Node ID from waitNbrs.\n");
	}
	//get the number of neighbors
	if ((nbrNum = topology_getNbrNum()) < 0) {
		printf("Error getting the number of neighbors from waitNbrs.\n");
	}
	//get neighbors with larger node IDs
	for (i = 0; i < nbrNum; i++) {
		if (nt[i].nodeID > myNodeID) 
			nbrsWithLargerIDs++;
	}

	if (nbrsWithLargerIDs == 0){
		printf("No neighbors with larger node IDs.\n\n");
		pthread_exit(NULL);
	}


	//listen for connections
	int tcpserv_sd;
	struct sockaddr_in tcpserv_addr;
	struct sockaddr_in tcpclient_addr;
	socklen_t tcpclient_addr_len = sizeof(struct sockaddr_in);
	tcpserv_sd = socket(AF_INET, SOCK_STREAM, 0); 
	if(tcpserv_sd<0) 
		printf("Error creating socket in waitNbrs.\n");
	memset(&tcpserv_addr, 0, sizeof(tcpserv_addr));
	tcpserv_addr.sin_family = AF_INET;
	struct in_addr temp = getMyIP();
	tcpserv_addr.sin_addr.s_addr = temp.s_addr;
	tcpserv_addr.sin_port = htons(CONNECTION_PORT);

	//set SO_REUSEADDR to TRUE
	int optval = 1;
	setsockopt(tcpserv_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	//bind and listen
	if(bind(tcpserv_sd, (struct sockaddr *)&tcpserv_addr, sizeof(tcpserv_addr))< 0) {
		printf("Error binding socket in waitNbrs (%s).\n", strerror(errno));
		nt_destroy(nt);
		exit(EXIT_FAILURE);
	}
	if(listen(tcpserv_sd, 1) < 0) 
		printf("Error listening to socket in waitNbrs.\n");
	printf("Waiting for connections from neighbors..\n\n");

	//accept nbrNum connections
	int acceptedConnections = 0;
	int newConnfd = -1;
	while (acceptedConnections < nbrsWithLargerIDs) {
		newConnfd = accept(tcpserv_sd,(struct sockaddr*)&tcpclient_addr,&tcpclient_addr_len);
		acceptedConnections++;
		if (newConnfd < 0) {
			printf("Error accepting connection in waitNbrs!\n");
		} else {
			//find the nbr entry in question and set its conn appropriately
			int tries = 0;
			//char *z = inet_ntoa(*(struct in_addr *)&tcpclient_addr.sin_addr.s_addr);
  			//printf("Trying to find neighbor entry matching IP %s (waitNbrs).\n", z);
			for (i = 0; i < nbrNum; i++) {
				if (nt[i].nodeIP == tcpclient_addr.sin_addr.s_addr) {
					nt[i].conn = newConnfd;
					printf("Neighbor with nodeID %d connected on connfd %d.\n", nt[i].nodeID, nt[i].conn);
					break;
				} else {
					tries++;
				}
			}
			if (tries == nbrNum){
				char *z = inet_ntoa(*(struct in_addr *)&tcpclient_addr.sin_addr.s_addr);
  				printf("Couldn't find IP %s in neighbor table (waitNbrs).\n", z);
			}
		}
	}

	pthread_exit(NULL);
}

// This function connects to all the neighbors that have a smaller node ID than my nodeID
// After all the outgoing connections are established, return 1, otherwise return -1
int connectNbrs() {
	int myNodeID, nbrNum;
	int i = 0;
	int nbrsWithSmallerIDs = 0;

	//make sure there's a neighbor table
	if (!nt) {
		nt = nt_create();
	}
	//get my node ID
	if ((myNodeID = topology_getMyNodeID()) < 0) {
		printf("Error getting my Node ID from waitNbrs.\n");
	}
	//get the number of neighbors
	if ((nbrNum = topology_getNbrNum()) < 0) {
		printf("Error gett the number of neighbors from waitNbrs.\n");
	}
	//get neighbors with smaller node IDs
	for (i = 0; i < nbrNum; i++) {
		if (nt[i].nodeID < myNodeID) 
			nbrsWithSmallerIDs++;
	}

	//setup
	int out_conn;
	int connections = 0;
	i = 0;
 	struct sockaddr_in servaddr;
 	servaddr.sin_family = AF_INET;
 	servaddr.sin_port = htons(CONNECTION_PORT);

 	//connect to each neighbor with a smaller node ID
 	printf("Trying to connect to %d neighbors with smaller node IDs.\n", nbrsWithSmallerIDs);
 	while (connections < nbrsWithSmallerIDs) {
		if (nt[i].nodeID < myNodeID) {
			servaddr.sin_addr.s_addr = nt[i].nodeIP;

 			out_conn = socket(AF_INET,SOCK_STREAM,0);
 			if (out_conn < 0) {
 				printf("Error creating socket in connectNbrs\n");
 				return -1;
 			}
 			if(connect(out_conn, (struct sockaddr*)&servaddr, sizeof(servaddr))<0) {
 				printf("Error connecting to socket in connectNbrs\n");
				return -1; 
			}
 			nt[i].conn = out_conn;
 			printf("Connected to neighbor with node ID %d (mine is %d) on conn %d.\n", nt[i].nodeID, myNodeID, nt[i].conn);
 			connections++;
 		}
 		i++;
 	}

	printf("Successfully connected to %d neighbors with smaller node IDs!\n\n", connections);
	return 1;
}

//Each listen_to_neighbor thread keeps receiving packets from a neighbor. It handles the received packets by forwarding the packets to the SNP process.
//all listen_to_neighbor threads are started after all the TCP connections to the neighbors are established 
void* listen_to_neighbor(void* arg) {
	int idx = *(int*)arg;
	snp_pkt_t packet;

	while (1) {

		//receive packet
		if (recvpkt(&packet, nt[idx].conn) < 0) {
			//printf("Closing connection to neighbor with nodeID %d.\n", nt[idx].nodeID);
			//close(nt[idx].conn);
			printf("\n\nCouldn't receive packet. Exiting listen_to_neighbor thread with nodeID %d.\n\n", nt[idx].nodeID);
			nt[idx].conn = -1;
			pthread_exit(NULL);
		}

		//if not connected to SNP yet, continue
		if (initialConnectToSNP == 0)
			continue;

		//forward packet to SNP
		printf("Received packet from node %d. Forwarding to SNP.\n", nt[idx].nodeID);
		if (network_conn == -2) {
			printf("\n\nCouldn't connect to SNP. Exiting listen_to_neighbor thread with nodeID %d.\n\n", nt[idx].nodeID);
			pthread_exit(NULL);
		} else if (forwardpktToSNP(&packet, network_conn) < 0) {
			printf("Error forwarding packet to SNP! (from neighbor with nodeID %d)\n", nt[idx].nodeID);
		}
	}
	nt[idx].conn = -1;
	pthread_exit(NULL);
}

//This function opens a TCP port on OVERLAY_PORT, and waits for the incoming connection from local SNP process. 
//After the local SNP process is connected, this function keeps getting sendpkt_arg_ts from SNP process, and 
//sends the packets to the next hop in the overlay network. If the next hop's nodeID is BROADCAST_NODEID,
// the packet should be sent to all the neighboring nodes.
void* waitNetwork(void* arg) {
	
	//open TCP port on OVERLAY_PORT and accept connection from local SNP
	int tcpserv_sd;
	struct sockaddr_in tcpserv_addr;
	struct sockaddr_in tcpclient_addr;
	socklen_t tcpclient_addr_len;
	tcpserv_sd = socket(AF_INET, SOCK_STREAM, 0);
	if(tcpserv_sd<0) 
		printf("Error creating socket in waitNetwork.\n");
	memset(&tcpserv_addr, 0, sizeof(tcpserv_addr));
	tcpserv_addr.sin_family = AF_INET;
	tcpserv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcpserv_addr.sin_port = htons(OVERLAY_PORT);
	//set SO_REUSEADDR to TRUE
	int optval = 1;
	setsockopt(tcpserv_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	if(bind(tcpserv_sd, (struct sockaddr *)&tcpserv_addr, sizeof(tcpserv_addr)) < 0) {
		printf("Error binding socket in waitNetwork (%s).\n", strerror(errno));
		nt_destroy(nt);
		exit(EXIT_FAILURE);
	}
	if(listen(tcpserv_sd, 1) < 0) 
		printf("Error listening to socket in waitNetwork.\n");
	printf("Waiting for connection from local SNP..\n");
	network_conn = accept(tcpserv_sd,(struct sockaddr*)&tcpclient_addr,&tcpclient_addr_len);
	if (network_conn < 0){
		printf("Error accepting connection from local SNP in waitNetwork.\n");
	} else {
		printf("Successfully connected to local SNP!\n\n");
		initialConnectToSNP = 1;
	}
	//get pkts from SNP process and forward them to the next node specified
	int nextNode, i;
	snp_pkt_t packet;
	int nbrNum = topology_getNbrNum();
	while (1) {
		//receive packet from SNP
		if (getpktToSend(&packet, &nextNode, network_conn) < 0) {
			printf("Error getting packet from SNP. Exiting waitNetwork thread.\n");
			//close(network_conn);
			network_conn = -2;
			pthread_exit(NULL);
		}
		if (nextNode == BROADCAST_NODEID) {
			//send packet to all neighbors
			printf("Broadcasting packet.\n");
			for (i = 0; i < nbrNum; i++) {
				printf("Sending packet to node ID %d.\n", nt[i].nodeID);
				if (sendpkt(&packet, nt[i].conn) < 0) {
					printf("Couldn't broadcast to neighbor with nodeID %d\n", nt[i].nodeID);
				} 
			}
		} else {
			//forward packet to next node
			printf("Sending packet to node ID %d.\n", nextNode);
			//get appropriate conn
			int i;
			int connfd = -1;
			if ((nbrNum = topology_getNbrNum()) < 0) {
				printf("Error getting the number of neighbors from waitNetwork.\n");
			}
			for (i = 0; i < nbrNum; i++) {
				if (nt[i].nodeID == nextNode) 
					connfd = nt[i].conn;
			}
			if (sendpkt(&packet, connfd) < 0) {
				printf("Couldn't send packet to to neighbor with nodeID %d\n", nextNode);
			}
		}
	}
	pthread_exit(NULL);
}

//this function stops the overlay
//it closes all the connections and frees all the dynamically allocated memory
//it is called when receiving a signal SIGINT
void overlay_stop() {
	nt_destroy(nt);
	close(network_conn);
	printf("Exiting from overlay_stop()\n");
	exit(EXIT_SUCCESS);
}

int main() {
	initialConnectToSNP = 0;
	//start overlay initialization
	printf("Overlay: Node %d initializing...\n",topology_getMyNodeID());	

	//create a neighbor table
	nt = nt_create();
	//initialize network_conn to -1, means no SNP process is connected yet
	network_conn = -1;
	
	//register a signal handler which is sued to terminate the process
	signal(SIGINT, overlay_stop);

	//print out all the neighbors
	int nbrNum = topology_getNbrNum();
	int i;
	for(i=0;i<nbrNum;i++) {
		printf("Overlay: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//start the waitNbrs thread to wait for incoming connections from neighbors with larger node IDs
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//wait for other nodes to start
	sleep(OVERLAY_START_DELAY);
	
	//connect to neighbors with smaller node IDs
	connectNbrs();

	//wait for waitNbrs thread to return
	pthread_join(waitNbrs_thread,NULL);	

	//at this point, all connections to the neighbors are created
	
	//create threads listening to all the neighbors
	for(i=0;i<nbrNum;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("Overlay: node initialized...\n");
	printf("Overlay: waiting for connection from SNP process...\n");

	//waiting for connection from  SNP process
	pthread_t waitNetwork_thread;
	pthread_create(&waitNetwork_thread,NULL,waitNetwork,(void*)0);

	while (1) {
		sleep(60);
	}
}
