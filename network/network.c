//FILE: network/network.c
//
//Description: this file implements network layer process  
//
//Date: April 29,2008



#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "network.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//network layer waits this time for establishing the routing paths 
#define NETWORK_WAITTIME 30

/**************************************************************/
//delare global variables
/**************************************************************/
int overlay_conn; 			//connection to the overlay
int transport_conn;			//connection to the transport
nbr_cost_entry_t* nct;			//neighbor cost table
dv_t* dv;				//distance vector table
pthread_mutex_t* dv_mutex;		//dvtable mutex
routingtable_t* routingtable;		//routing table
pthread_mutex_t* routingtable_mutex;	//routingtable mutex


/**************************************************************/
//implementation network layer functions
/**************************************************************/

//This function is used to for the SNP process to connect to the local ON process on port OVERLAY_PORT.
//TCP descriptor is returned if success, otherwise return -1.
int connectToOverlay() { 
	//setup
	int out_conn;
 	struct sockaddr_in servaddr;
 	struct hostent *hostInfo;
	hostInfo = gethostbyname("localhost");
 	if(!hostInfo) {
		printf("host name error!\n");
		return -1;
	}
 	servaddr.sin_family = AF_INET;
 	servaddr.sin_port = htons(OVERLAY_PORT);
	memcpy((char *) &servaddr.sin_addr.s_addr, hostInfo->h_addr_list[0], hostInfo->h_length);


	//connect to overlay port
	printf("Trying to connect to overlay.\n");
	out_conn = socket(AF_INET,SOCK_STREAM,0);
	if (out_conn < 0) {
		printf("Error creating socket in connectToOverlay.\n");
		return -1;
	}
	if(connect(out_conn, (struct sockaddr*)&servaddr, sizeof(servaddr))<0) {
		printf("Error connecting to socket in connectToOverlay.\n");
		return -1;
	}
	return out_conn;
}

//This thread sends out route update packets every ROUTEUPDATE_INTERVAL time
//The route update packet contains this node's distance vector. 
//Broadcasting is done by set the dest_nodeID in packet header as BROADCAST_NODEID
//and use overlay_sendpkt() to send the packet out using BROADCAST_NODEID address.
void* routeupdate_daemon(void* arg) {

	int numTotalNodes = topology_getNodeNum();
	snp_pkt_t packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.src_nodeID = topology_getMyNodeID();
	if (packet.header.src_nodeID <= 1 ) {
		printf("Getting myNodeID again in routeupdate_daemon!\n");
		packet.header.src_nodeID = topology_getMyNodeID();
	}
	packet.header.dest_nodeID = BROADCAST_NODEID;
	packet.header.type = ROUTE_UPDATE;
	packet.header.length = sizeof(pkt_routeupdate_t);
	int nextNodeID = BROADCAST_NODEID;

	// create route update packet
	dv_t *routeUpdateDV = getRouteUpdateData(dv, packet.header.src_nodeID);
	pkt_routeupdate_t routeUpdatePacket;
	routeUpdatePacket.entryNum = numTotalNodes;
	for (int i = 0; i < numTotalNodes; i++) {
		routeUpdatePacket.entry[i].nodeID = routeUpdateDV->dvEntry[i].nodeID;
		routeUpdatePacket.entry[i].cost = routeUpdateDV->dvEntry[i].cost;
	}
	memcpy(packet.data, &routeUpdatePacket, sizeof(pkt_routeupdate_t));
	
	while (overlay_sendpkt(nextNodeID, &packet, overlay_conn) > 0){
		printf("Broadcasting route update packet from node ID %d.\n", packet.header.src_nodeID);
		sleep(ROUTEUPDATE_INTERVAL);

		// NEW CODE - set route update data
		routeUpdateDV = getRouteUpdateData(dv, packet.header.src_nodeID);
		for (int i = 0; i < numTotalNodes; i++) {
		routeUpdatePacket.entry[i].nodeID = routeUpdateDV->dvEntry[i].nodeID;
		routeUpdatePacket.entry[i].cost = routeUpdateDV->dvEntry[i].cost;
		}
		memcpy(packet.data, &routeUpdatePacket, sizeof(pkt_routeupdate_t));

		if (overlay_conn == -2)
			break;
	}
	printf("Exiting routeupdate thread.\n");
	pthread_exit(NULL);
}

//This thread handles incoming packets from the ON process.
//It receives packets from the ON process by calling overlay_recvpkt().
//If the packet is a SNP packet and the destination node is this node, forward the packet to the SRT process.
//If the packet is a SNP packet and the destination node is not this node, forward the packet to the next hop
	// according to the routing table.
//If this packet is an Route Update packet, update the distance vector table and the routing table. 
void* pkthandler(void* arg) {
	snp_pkt_t packet;
	char *packetTypes[] = {"Unknown", "ROUTE_UPDATE", "SNP"};
	int myNodeID = topology_getMyNodeID();
	if (myNodeID <= 1 ) {
		printf("Getting myNodeID again in pkthandler!\n");
		myNodeID = topology_getMyNodeID();
	}
	int numTotalNodes = topology_getNodeNum();
	dv_t *myDV = getRouteUpdateData(dv, myNodeID);
	int nextNode;

	while(overlay_recvpkt(&packet,overlay_conn) > 0) {
		printf("Routing: received %s packet from %d.\n", packetTypes[packet.header.type], packet.header.src_nodeID);
		
		if (packet.header.type == SNP) { //it's an SNP packet
			if (packet.header.dest_nodeID == myNodeID) {
				printf("dest_nodeID is %d. Forwarding to transport layer.\n", packet.header.dest_nodeID);
				if (forwardsegToSRT(transport_conn, packet.header.src_nodeID, (seg_t*)packet.data) < 0){
					printf("Error forwarding segment to SRT (from src_nodeID %d)\n", packet.header.src_nodeID);
				}
			} else {
				//get next node from routing table
				nextNode = routingtable_getnextnode(routingtable, packet.header.dest_nodeID);
				if(nextNode < 0)
					printf("Error getting next node for dest_nodeID %d.\n", packet.header.dest_nodeID);
				printf("dest_nodeID is %d. Forwarding to next node ID %d.\n", packet.header.dest_nodeID, nextNode);

				//ask overlay to send packet
				if (overlay_sendpkt(nextNode, &packet, overlay_conn) < 0) {
					printf("Couldn't send packet to overlay (to next node %d)\n", nextNode);
				}
			}
		} else { //it's a route update packet
			pkt_routeupdate_t routeUpdatePacket;
			memcpy(&routeUpdatePacket, packet.data, sizeof(pkt_routeupdate_t));
			/*printf("Printing route update packet...\n");
			for (int i = 0; i < numTotalNodes; i++) {
				printf("\tdest_nodeID ID %u cost %u\n", routeUpdatePacket.entry[i].nodeID, routeUpdatePacket.entry[i].cost);
			}*/

			//get appropriate dv_t
			dv_t *routeUpdateDV = getRouteUpdateData(dv, packet.header.src_nodeID);
			/*printf("Printing corresponding dv_t entries...\n");
			for (int i = 0; i < numTotalNodes; i++) {
				printf("\tdest_nodeID ID %u cost %u\n", routeUpdateDV->dvEntry[i].nodeID, routeUpdateDV->dvEntry[i].cost);
			}*/

			int needToUpdateDV = 0;

			//check to see if different - if so, change dv_t cost to routeUpdatePacket cost
			for (int i = 0; i < numTotalNodes; i++) {
				if (routeUpdatePacket.entry[i].nodeID == routeUpdateDV->dvEntry[i].nodeID &&
						routeUpdatePacket.entry[i].cost != routeUpdateDV->dvEntry[i].cost) {
					printf("Changing nodeID %d cost from %u to %u.\n", routeUpdateDV->dvEntry[i].nodeID,
						routeUpdateDV->dvEntry[i].cost, routeUpdatePacket.entry[i].cost);
					pthread_mutex_lock(dv_mutex);
					routeUpdateDV->dvEntry[i].cost = routeUpdatePacket.entry[i].cost;
					pthread_mutex_unlock(dv_mutex);
				needToUpdateDV = 1;
				}
			}

			//update DV and routing tables as needed
			if (needToUpdateDV) {
				printf("Updated distance vector table.\n");
				dvtable_print(dv);
				int newMin;

				//for all nodes in network, update cost
				for (int i = 0; i < numTotalNodes; i++) {
					newMin = min( myDV->dvEntry[i].cost, nbrcosttable_getcost(nct, packet.header.src_nodeID) + routeUpdateDV->dvEntry[i].cost);
					//if we've found a shorter path...
					if (newMin < myDV->dvEntry[i].cost) {
						printf("Updating DV table and routing table.\n");
						pthread_mutex_lock(dv_mutex);
						pthread_mutex_lock(routingtable_mutex);
						//update DV
						myDV->dvEntry[i].cost = newMin;
						//update routing table
						routingtable_setnextnode(routingtable, myDV->dvEntry[i].nodeID, packet.header.src_nodeID);
						pthread_mutex_unlock(dv_mutex);
						pthread_mutex_unlock(routingtable_mutex);

						printf("Updated DV table and routing table.\n");
						dvtable_print(dv);
						routingtable_print(routingtable);


						//broadcast new DV to neighbors
						snp_pkt_t broadcastPacket;
						memset(&broadcastPacket, 0, sizeof(packet));
						broadcastPacket.header.src_nodeID = topology_getMyNodeID();
						if (broadcastPacket.header.src_nodeID < 0 )
							printf("Couldn't get my Node ID in pkthandler!\n");
						broadcastPacket.header.dest_nodeID = BROADCAST_NODEID;
						broadcastPacket.header.type = ROUTE_UPDATE;
						broadcastPacket.header.length = sizeof(pkt_routeupdate_t);
						int nextNodeID = BROADCAST_NODEID;
						dv_t *routeUpdateDV = getRouteUpdateData(dv, broadcastPacket.header.src_nodeID);
						pkt_routeupdate_t routeUpdatePacket;
						routeUpdatePacket.entryNum = numTotalNodes;
						for (int i = 0; i < numTotalNodes; i++) {
							routeUpdatePacket.entry[i].nodeID = routeUpdateDV->dvEntry[i].nodeID;
							routeUpdatePacket.entry[i].cost = routeUpdateDV->dvEntry[i].cost;
						}
						memcpy(broadcastPacket.data, &routeUpdatePacket, sizeof(pkt_routeupdate_t));
						if (overlay_sendpkt(nextNodeID, &broadcastPacket, overlay_conn) > 0)
							printf("Broadcasted updated DV to neighbors!\n");
						else
							printf("Error broadcasting updated DV to neighbors from pkthandler!\n");
					}
				}
			}
		}
	}
	printf("Exiting pkthandler thread.\n");
	overlay_conn = -2;
	pthread_exit(NULL);
}

//This function stops the SNP process. 
//It closes all the connections and frees all the dynamically allocated memory.
//It is called when the SNP process receives a signal SIGINT.
void network_stop() {
	close(overlay_conn);
	close(transport_conn);
	printf("Closing the connections with overlay and transport.\n");

	pthread_mutex_destroy(dv_mutex);
	free(dv_mutex);
	pthread_mutex_destroy(routingtable_mutex);
	free(routingtable_mutex);
	printf("Mutexes destroyed and freed.\n");

	routingtable_destroy(routingtable);
	nbrcosttable_destroy(nct);
	dvtable_destroy(dv);

	exit(EXIT_SUCCESS);
}

//This function opens a port on NETWORK_PORT and waits for the TCP connection from local SRT process.
//After the local SRT process is connected, this function keeps receiving sendseg_arg_ts which contains
// the segments and their destination node addresses from the SRT process. The received segments are then
// encapsulated into packets (one segment in one packet), and sent to the next hop using overlay_sendpkt. 
//The next hop is retrieved from routing table.
//When a local SRT process is disconnected, this function waits for the next SRT process to connect.
void waitTransport() {
	int tcpserv_sd;
	struct sockaddr_in tcpserv_addr;
	struct sockaddr_in tcpclient_addr;
	socklen_t tcpclient_addr_len;

	tcpserv_sd = socket(AF_INET, SOCK_STREAM, 0); 
	if(tcpserv_sd<0) 
		printf("Error creating socket in waitTransport()\n");	
	memset(&tcpserv_addr, 0, sizeof(tcpserv_addr));
	tcpserv_addr.sin_family = AF_INET;
	tcpserv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcpserv_addr.sin_port = htons(NETWORK_PORT);

	//set SO_REUSEADDR to TRUE
	int optval = 1;
	setsockopt(tcpserv_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	if(bind(tcpserv_sd, (struct sockaddr *)&tcpserv_addr, sizeof(tcpserv_addr))< 0)
		printf("Error binding socket in waitTransport()\n"); 
	if(listen(tcpserv_sd, 1) < 0) 
		printf("Error listening to socket in waitTransport()\n");
	printf("Waiting for transport connection from SRT\n");
	transport_conn = -1;
	transport_conn = accept(tcpserv_sd,(struct sockaddr*)&tcpclient_addr,&tcpclient_addr_len);
	if (transport_conn < 1)
		printf("Error accepting connection from SRT.\n");


	int dest_nodeID, nextNode;
	seg_t segment;
	snp_pkt_t packet;
	packet.header.src_nodeID = topology_getMyNodeID();
	packet.header.type = SNP;
	packet.header.length = sizeof(seg_t);
	while (1) {
		//receive sendseg_arg_t from SRT transport
		if (getsegToSend(transport_conn, &dest_nodeID, &segment) < 0) {
			printf("Error getting segment from SRT. Closing transport_conn and listening for additional SRT connections.\n");
			close(transport_conn);
			transport_conn = -1;
			transport_conn = accept(tcpserv_sd,(struct sockaddr*)&tcpclient_addr,&tcpclient_addr_len);
			if (transport_conn < 1)
				printf("Error accepting another connection from SRT.\n");
		}

		//encapsulate in packet
		packet.header.dest_nodeID = dest_nodeID;
		memset(packet.data, 0, MAX_PKT_LEN);
		memcpy(packet.data, &segment, sizeof(segment));

		//get next node from routing table
		nextNode = routingtable_getnextnode(routingtable, dest_nodeID);
		if(nextNode < 0)
			printf("Error getting next node for dest_nodeID %d.\n", dest_nodeID);
		printf("Sending packet with dest_nodeID %d to node ID %d.\n", dest_nodeID, nextNode);

		//ask overlay to send packet
		if (overlay_sendpkt(nextNode, &packet, overlay_conn) < 0) {
			printf("Couldn't send packet to overlay (to next node %d)\n", nextNode);
		}

	}
	pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
	printf("network layer is starting, pls wait...\n");

	//initialize global variables
	nct = nbrcosttable_create();
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	overlay_conn = -1;
	transport_conn = -1;

	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//register a signal handler which is used to terminate the process
	signal(SIGINT, network_stop);

	//connect to local ON process 
	overlay_conn = connectToOverlay();
	if(overlay_conn<0) {
		printf("can't connect to overlay process\n");
		exit(1);		
	}
	
	//start a thread that handles incoming packets from ON process 
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//start a route update thread 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("network layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(NETWORK_WAITTIME);
	routingtable_print(routingtable);

	//wait connection from SRT process
	printf("waiting for connection from SRT process\n");
	waitTransport(); 

}


