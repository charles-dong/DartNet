//
// FILE: srt_client.c
//
// Description: this file contains client states' definition, some important data structures
// and the client SRT socket interface definitions. You need to implement all these interfaces
//
// Date: April 18, 2008
//       April 21, 2008 **Added more detailed description of prototypes fixed ambiguities** ATC
//       April 26, 2008 ** Added GBN and send buffer function descriptions **
//

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "srt_client.h"
#include "../topology/topology.h"

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif


unsigned long current_utc_time_ns(struct timespec *ts) {
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  ts->tv_sec = mts.tv_sec;
  ts->tv_nsec = mts.tv_nsec;
  return ts->tv_nsec + ts->tv_sec*NANOSECONDS_PER_SECOND;
#else
  clock_gettime(CLOCK_REALTIME, ts);
  return ts->tv_nsec;
#endif
}


// global variables
int overlay_conn_fd; // for the overlay TCP socket descriptor ‘‘conn’’ used as input parameter for snp_sendseg and snp_recvseg
client_tcb_t *client_TCB_Table[MAX_TRANSPORT_CONNECTIONS];


//
//
//  SRT socket API for the client side application. 
//  ===================================
//
//  In what follows, we provide the prototype definition for each call and limited pseudo code representation
//  of the function. This is not meant to be comprehensive - more a guideline. 
// 
//  You are free to design the code as you wish.
//
//  NOTE: When designing all functions you should consider all possible states of the FSM using
//  a switch statement (see the Lab4 assignment for an example). Typically, the FSM has to be
// in a certain state determined by the design of the FSM to carry out a certain action. 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

// This function initializes the TCB table marking all entries NULL. It also initializes 
// a global variable for the overlay TCP socket descriptor ``conn'' used as input parameter
// for snp_sendseg and snp_recvseg. Finally, the function starts the seghandler thread to 
// handle the incoming segments. There is only one seghandler for the client side which
// handles call connections for the client.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void srt_client_init(int conn)
{
	//Initialize global overaly TCP socket descriptor for sendseg and recvseg
	overlay_conn_fd = conn;

	// instantiation of seghandler thread
	//there's only one seghandler for the client side. Start seghandler thread to handle incoming segments
	pthread_t segHandlerThread;
    if (pthread_create(&segHandlerThread, NULL, seghandler, NULL)){
    	printf("Error creating seghandler thread.\n");
    }

	printf("Initialized client.\n");
}


// This function looks up the client TCB table to find the first NULL entry, and creates
// a new TCB entry using malloc() for that entry. All fields in the TCB are initialized 
// e.g., TCB state is set to CLOSED and the client port set to the function call parameter 
// client port.  The TCB table entry index should be returned as the new socket ID to the client 
// and be used to identify the connection on the client side. If no entry in the TC table  
// is available the function returns -1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_sock(unsigned int client_port)
{
	//malloc new TCB entry at first NULL entry
	int sockfd = 0;
	while (sockfd < MAX_TRANSPORT_CONNECTIONS){
		if (client_TCB_Table[sockfd] == NULL) {

			//malloc new client tcb entry
			client_TCB_Table[sockfd] = malloc(sizeof(client_tcb_t));
			MALLOC_CHECK(client_TCB_Table[sockfd]);
			memset(client_TCB_Table[sockfd], 0, sizeof(client_tcb_t));

			//initialize TCB entry
			client_TCB_Table[sockfd]->client_portNum = client_port;
			client_TCB_Table[sockfd]->state = CLOSED;
			client_TCB_Table[sockfd]->next_seqNum = 0;
			client_TCB_Table[sockfd]->bufMutex = malloc(sizeof(pthread_mutex_t));
			memset(client_TCB_Table[sockfd]->bufMutex, 0, sizeof(pthread_mutex_t));
			client_TCB_Table[sockfd]->sendBufHead = NULL;
			client_TCB_Table[sockfd]->sendBufTail = NULL;
			client_TCB_Table[sockfd]->sendBufunSent = NULL;
			client_TCB_Table[sockfd]->unAck_segNum = 0;
			client_TCB_Table[sockfd]->client_nodeID = topology_getMyNodeID(); //new
			printf("My nodeID is %u.\n", client_TCB_Table[sockfd]->client_nodeID);

			//initialize mutex
			if (pthread_mutex_init(client_TCB_Table[sockfd]->bufMutex, NULL) != 0) {
			    printf("\n mutex init failed\n");
			    return -1;
			}

			break;
		} else {
			sockfd++;
		}
	}

	if (sockfd == MAX_TRANSPORT_CONNECTIONS) {
		printf("You've reached the maximum number of transport connections.\n");
		return -1;
	} else {
		printf("Created new TCB client entry with sockfd %d.\n", sockfd);
		return sockfd;
	}
}


// This function is used to connect to the server. It takes the socket ID and the 
// server's port number as input parameters. The socket ID is used to find the TCB entry.  
// This function sets up the TCB's server port number and a SYN segment to send to
// the server using snp_sendseg(). After the SYN segment is sent, a timer is started. 
// If no SYNACK is received after SYNSEG_TIMEOUT timeout, then the SYN is 
// retransmitted. If SYNACK is received, return 1. Otherwise, if the number of SYNs 
// sent > SYN_MAX_RETRY,  transition to CLOSED state and return -1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_connect(int sockfd, int nodeID, unsigned int server_port)
{
	//find TCB entry
	client_tcb_t *currentTCB = client_TCB_Table[sockfd];
	if (currentTCB == NULL){
		printf("Couldn't find the specified client TCB entry.\n");
		return -1;
	}

	switch(currentTCB->state) {
		case CLOSED:
		  printf("Trying to connect to server.\n");
		  break;

		case SYNSENT:
		  printf("State is SYNSENT. Can't connect.\n");
		  return -1;

		case CONNECTED:
		  printf("State is CONNECTED. Can't connect.\n");
		  return -1;

		case FINWAIT:
		  printf("State is FINWAIT. Can't connect.\n");
		  return -1;

		default:
		  printf("Unknown state. Can't connect.\n");
		  return -1;
	}	

	//set up server port number
	currentTCB->svr_portNum = server_port;
	currentTCB->svr_nodeID = nodeID; //new

	//create SYN seg_t
	seg_t* synSegPtr = malloc(sizeof(seg_t));
	MALLOC_CHECK(synSegPtr);
	memset(synSegPtr, 0, sizeof(seg_t));
	synSegPtr->header.src_port = currentTCB->client_portNum;
	synSegPtr->header.dest_port = currentTCB->svr_portNum;
	synSegPtr->header.type = SYN;
	synSegPtr->header.seq_num = currentTCB->next_seqNum;

	currentTCB->state = SYNSENT;
	//send SYN seg_t
	if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, synSegPtr) < 0) {
		printf("Error sending SYN seg_t.\n");
		return -1;
	}


	//start timer
	struct timespec ts;
	unsigned long  tstart, tend;
	tstart = current_utc_time_ns(&ts);
	int tries = 0;

	//keep trying until receive SYNACK (seghandler changes currentTCB.state to CONNECTED) or max out tries
	while (tries < SYN_MAX_RETRY && currentTCB->state == SYNSENT) {
		//calc time elapsed
		tend = current_utc_time_ns(&ts);
		if (tstart > tend) 
			tend += 1000000000;
		
		//if timeout, resend SYN
		if (labs(tend - tstart) > SYN_TIMEOUT) {
			tstart = current_utc_time_ns(&ts);
			//resend SYN and increment tries
			printf("Resending SYN.\n");
			if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, synSegPtr) < 0) {
				printf("Error sending SYN seg_t.\n");
				return -1;
			}
			tries++;
		}
	}

	free(synSegPtr);
	//if SYNACK is received, seghandler will change state to CONNECTED
	if (currentTCB->state == CONNECTED) {
		printf("We're CONNECTED!\n");
		return 1;
	} else {
		printf("Couldn't connect. Switching to CLOSED.\n");
		currentTCB->state = CLOSED;
		return -1;
	}
}


// Send data to a srt server. This function should use the socket ID to find the TCP entry. 
// Then It should create segBufs using the given data and append them to send buffer linked list. 
// If the send buffer was empty before insertion, a thread called sendbuf_timer 
// should be started to poll the send buffer every SENDBUF_POLLING_INTERVAL time
// to check if a timeout event should occur. If the function completes successfully, 
// it returns 1. Otherwise, it returns -1.
// 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_send(int sockfd, void* data, unsigned int length)
{
  
	//find TCB entry
	client_tcb_t *currentTCB = client_TCB_Table[sockfd];
	if (currentTCB == NULL){
		printf("Couldn't find the specified client TCB entry.\n");
		return -1;
	}
	switch(currentTCB->state) {
		case CLOSED:
		  printf("State is CLOSED. Can't send.\n");
		  return -1;

		case SYNSENT:
		  printf("State is SYNSENT. Can't send.\n");
		  return -1;

		case CONNECTED:
		  printf("\nAdding segBufs to queue to send to %u.\n", currentTCB->svr_portNum);
		  break;

		case FINWAIT:
		  printf("State is FINWAIT. Can't send.\n");
		  return -1;

		default:
		  printf("Unknown state. Can't send.\n");
		  return -1;
	}

	//how many segments do we need?
	char *dataToTransmit = (char *)data;
	unsigned int dataLength = length;
	unsigned int numOfSegments = dataLength / MAX_SEG_LEN;
	unsigned int dataRemainder = dataLength % MAX_SEG_LEN;
	if (dataRemainder > 0 ){
		numOfSegments++;
	}
	if (numOfSegments <= 0) {
		printf("No data to send. Returning to application.\n");
		return 1;
	}

	//create segBuf structs and append to sendBuffer
	while (numOfSegments > 0) {

		//create segBuf
		segBuf_t *currentSegBuf = malloc(sizeof(segBuf_t));
		memset(currentSegBuf, 0, sizeof(segBuf_t));
		currentSegBuf->next = NULL;

		//copy data and increment data pointer to next segment
			//also update header.length
		if (numOfSegments > 1) { //if it's a full segment...
			memcpy(currentSegBuf->seg.data, dataToTransmit, MAX_SEG_LEN);
			dataToTransmit += MAX_SEG_LEN;
			currentSegBuf->seg.header.length = MAX_SEG_LEN;
		} else { //if it's a partial segment..
			memcpy(currentSegBuf->seg.data, dataToTransmit, dataRemainder);
			//currentSegBuf->seg.data[dataRemainder] = '\0';
			currentSegBuf->seg.header.length = dataRemainder;// + 1;
		}

		//write header
		currentSegBuf->seg.header.src_port = currentTCB->client_portNum;
		currentSegBuf->seg.header.dest_port = currentTCB->svr_portNum;
		currentSegBuf->seg.header.seq_num = currentTCB->next_seqNum;
		currentTCB->next_seqNum += currentSegBuf->seg.header.length;
		currentSegBuf->seg.header.type = DATA;

		//add it to the appropriate space in the queue
		pthread_mutex_lock(currentTCB->bufMutex);
		if (currentTCB->sendBufHead == NULL) { //this is the first segment
			currentTCB->sendBufHead = currentSegBuf;
			currentTCB->sendBufTail = currentSegBuf;
			currentTCB->sendBufunSent = currentSegBuf;

			pthread_t sendBufTimeoutThread;
		    if (pthread_create(&sendBufTimeoutThread, NULL, sendBuf_timer, currentTCB)){
		    	printf("Error creating closeWaitTimer thread.\n");
		    }
		} else { //there's already stuff in the buffer
			currentTCB->sendBufTail->next = currentSegBuf;
			currentTCB->sendBufTail = currentSegBuf;
			if (currentTCB->sendBufunSent == NULL) {
				currentTCB->sendBufunSent = currentSegBuf;
			}
		}

		//printf("Sending from srt_client_send.\n");
		/* printing queue
		currentSegBuf = currentTCB->sendBufHead;
		while (currentSegBuf != NULL) {
			printf("seq_num: %u, data: %s\n", currentSegBuf->seg.header.seq_num, currentSegBuf->seg.data);
			currentSegBuf = currentSegBuf->next;
		} */
		pthread_mutex_unlock(currentTCB->bufMutex);

		numOfSegments--;
	}

	//send segments until sent-but-not-Acked segments reaches GBN_WINDOW or segments are all sent
	if (sendMaxSegments(currentTCB) < 0) {
		printf("Error sending initial segments from srt_client_send.\n");
	}

	return 1;
}

 //sends unsent segments until GBN_WINDOW or end of queue and updates currentTCB->unAck_segNum
	//returns 1 for success or -1 for failure
int sendMaxSegments(client_tcb_t *currentTCB){

	struct timespec ts;
	pthread_mutex_lock(currentTCB->bufMutex);
	segBuf_t *currentSegBuf = currentTCB->sendBufunSent;

	//send segBufs
	while (currentTCB->unAck_segNum <= GBN_WINDOW && currentSegBuf != NULL){
		currentSegBuf->sentTime = current_utc_time_ns(&ts) / NS_TO_MICROSECONDS;
		if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, &currentSegBuf->seg) < 0) {
			printf("Error sending seg_t with seq_num %u.\n", currentSegBuf->seg.header.seq_num);
			return -1;
		} else {
			printf("Sent seq_num %u. Client: %u, Server: %u\n", currentSegBuf->seg.header.seq_num, 
				currentSegBuf->seg.header.src_port, currentSegBuf->seg.header.dest_port);
		}
		currentTCB->unAck_segNum++;
		currentSegBuf = currentSegBuf->next;
	}

	if (currentSegBuf != NULL) {
		currentTCB->sendBufunSent = currentSegBuf;
	} else {
		currentTCB->sendBufunSent = NULL;
	}

	pthread_mutex_unlock(currentTCB->bufMutex);
	return 1;
}




// This function is used to disconnect from the server. It takes the socket ID as 
// an input parameter. The socket ID is used to find the TCB entry in the TCB table.  
// This function sends a FIN segment to the server. After the FIN segment is sent
// the state should transition to FINWAIT and a timer started. If the 
// state == CLOSED after the timeout the FINACK was successfully received. Else,
// if after a number of retries FIN_MAX_RETRY the state is still FINWAIT then
// the state transitions to CLOSED and -1 is returned.


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_disconnect(int sockfd)
{
  	//find TCB entry
	client_tcb_t *currentTCB = client_TCB_Table[sockfd];
	if (currentTCB == NULL){
		printf("Couldn't find the specified client TCB entry.\n");
		return -1;
	}

	switch(currentTCB->state) {
		case CLOSED:
		  printf("Asked to disconnect, but the state is already CLOSED.\n");
		  return -1;

		case SYNSENT:
		  printf("Asked to disconnect, but the state is SYNSENT.\n");
		  return -1;

		case CONNECTED:
		    printf("Trying to disconnect.\n");

		    //clear send buffer
		    //printf("Clearing sendBuf.\n");
		    pthread_mutex_lock(currentTCB->bufMutex);

		    /*
		    segBuf_t *currentSegBuf = currentTCB->sendBufHead;
		    segBuf_t *tempSegBuf;

		    //send everything again
		    while (currentSegBuf != NULL){
		    	struct timespec ts;
				currentSegBuf->sentTime = current_utc_time_ns(&ts) / NS_TO_MICROSECONDS;
				if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, &currentSegBuf->seg) < 0) {
					printf("Error sending seg_t with seq_num %u.\n", currentSegBuf->seg.header.seq_num);
					return -1;
				} else {
					printf("Sent seg_t with seq_num %u.\n", currentSegBuf->seg.header.seq_num);
				}
				currentSegBuf = currentSegBuf->next;
			}
			*/

			//free all of the segbufs
			segBuf_t *currentSegBuf = currentTCB->sendBufHead;
			segBuf_t *tempSegBuf;
		    while (currentSegBuf != NULL) {
		    	tempSegBuf = currentSegBuf;
		    	currentSegBuf = currentSegBuf->next;
		    	free(tempSegBuf);
		    }
		    currentTCB->sendBufHead = NULL;
		    currentTCB->sendBufTail = NULL;
		    currentTCB->sendBufunSent = NULL;
		    pthread_mutex_unlock(currentTCB->bufMutex);

		  	//create FIN seg_t
			seg_t* finSegPtr = malloc(sizeof(seg_t));
			MALLOC_CHECK(finSegPtr);
			memset(finSegPtr, 0, sizeof(seg_t));
			finSegPtr->header.src_port = currentTCB->client_portNum;
			finSegPtr->header.dest_port = currentTCB->svr_portNum;
			finSegPtr->header.type = FIN;

			currentTCB->state = FINWAIT;
			//send FIN seg_t
			if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, finSegPtr) < 0) {
				printf("Error sending FIN seg_t to %u.\n", finSegPtr->header.dest_port);
				return -1;
			}


			//start timer
			struct timespec ts;
			unsigned long  tstart, tend;
			tstart = current_utc_time_ns(&ts);
			int tries = 0;

			//keep trying until receive FINACK (seghandler changes currentTCB.state to CLOSED) or max out tries
			while (tries < FIN_MAX_RETRY && currentTCB->state == FINWAIT) {
				//calc time elapsed
				tend = current_utc_time_ns(&ts);

				//if timeout, resend FIN
				if (tend - tstart> FIN_TIMEOUT) {
					tstart = current_utc_time_ns(&ts);
					//resend FIN and increment tries
					printf("Resending FIN to %u.\n", finSegPtr->header.dest_port);
					if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, finSegPtr) < 0) {
						printf("Error sending FIN seg_t to %u.\n", finSegPtr->header.dest_port);
						return -1;
					}
					tries++;
				}
			}

			//if FINACK is received, seghandler will change state to CLOSED
			if (currentTCB->state == CLOSED) {
				printf("Successful disconnection!\n");
				free(finSegPtr);
				return 1;
			} else {
				printf("Couldn't disconnect - maxed out tries. Switching to CLOSED.\n");
				currentTCB->state = CLOSED;
				free(finSegPtr);
				return -1;
			}

		case FINWAIT:
		  printf("Asked to disconnect, but the state is FINWAIT. Transitioning to CLOSED.\n");
		  return -1;

		default:
		  printf("Unknown state. Can't disconnect.\n");
		  return -1;
	}	
}


// This function calls free() to free the TCB entry. It marks that entry in TCB as NULL
// and returns 1 if succeeded (i.e., was in the right state to complete a close) and -1 
// if fails (i.e., in the wrong state).
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_close(int sockfd)
{
  //find TCB entry
	client_tcb_t *currentTCB = client_TCB_Table[sockfd];
	if (currentTCB == NULL){
		printf("Couldn't find the specified client TCB entry.\n");
		return -1;
	}

	switch(currentTCB->state) {
		case CLOSED:
		  printf("Trying to close.\n");
		  pthread_mutex_destroy(client_TCB_Table[sockfd]->bufMutex);
		  free(client_TCB_Table[sockfd]->bufMutex);
		  free(client_TCB_Table[sockfd]);
		  client_TCB_Table[sockfd] = NULL;
		  printf("Successfully closed!\n");
		  return 1;

		case SYNSENT:
		  printf("Asked to close, but the state is SYNSENT.\n");
		  return -1;

		case CONNECTED:
		  printf("Trying to close, but the state is CONNECTED.\n");
		  return -1;

		case FINWAIT:
		  printf("Asked to close, but the state is FINWAIT. Transitioning to CLOSED.\n");
		  return -1;

		default:
		  printf("Unknown state. Can't close.\n");
		  return -1;
	}
}


// This is a thread  started by srt_client_init(). It handles all the incoming 
// segments from the server. The design of seghanlder is an infinite loop that calls snp_recvseg(). If
// snp_recvseg() fails then the overlay connection is closed and the thread is terminated. Depending
// on the state of the connection when a segment is received  (based on the incoming segment) various
// actions are taken. See the client FSM for more details.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void *seghandler(void* arg)
{
  	seg_t *segPtr = malloc(sizeof(seg_t));
	MALLOC_CHECK(segPtr);
	memset(segPtr, 0, sizeof(seg_t));

	char *segTypeStrings[] = {"SYN", "SYNACK", "FIN", "FINACK", "DATA", "DATAACK"};
	char *states[] = {"Unknown", "CLOSED", "SYNSENT", "CONNECTED", "FINWAIT"};
	int src_nodeID;

	while (snp_recvseg(overlay_conn_fd, &src_nodeID, segPtr) > 0) {

		if(!segPtr) {
			printf("segPtr is NULL.\n");
			break;
		}
		

		//get the right client_tcb_t index
		unsigned int destPort = segPtr->header.dest_port;
		int idx = 0;
		while (idx < MAX_TRANSPORT_CONNECTIONS) {
			if (client_TCB_Table[idx] != NULL) {
				if (client_TCB_Table[idx]->client_portNum == destPort){
					break;
				}
			}
			idx++;
		}

		if (idx < MAX_TRANSPORT_CONNECTIONS){
			client_tcb_t *currentTCB = client_TCB_Table[idx];
			//printf("Sockfd is %d, and port is %u.\n", idx, segPtr->header.dest_port);
			printf("\nReceived %s in state %s. client: %u, server: %u.\n", 
				segTypeStrings[segPtr->header.type], states[currentTCB->state], segPtr->header.dest_port, segPtr->header.src_port);
			switch(currentTCB->state) {
				case CLOSED:
				  //printf("State is CLOSED.\n");
				  printf("Doing nothing.\n");
				  break;

				case SYNSENT:
				  //printf("State is SYNSENT.\n");
				  if (segPtr->header.type == SYNACK && currentTCB->svr_portNum == segPtr->header.src_port && currentTCB->svr_nodeID==src_nodeID){
				  	printf("Changing state to CONNECTED.\n");
				  	currentTCB->state = CONNECTED;
				  } else {
				  	printf("Doing nothing.\n");
				  }
				  break;

				case CONNECTED:
				  //printf("State is CONNECTED.\n");
				  if (segPtr->header.type == DATAACK && currentTCB->svr_portNum == segPtr->header.src_port && currentTCB->svr_nodeID==src_nodeID){
				  	printf("Server expects seq_num %u.\n", segPtr->header.seq_num);
				  	pthread_mutex_lock(currentTCB->bufMutex);
				  	segBuf_t *tempSegBuf;

				  	//free acked segBufs from send buffer
				  	while (currentTCB->sendBufHead != NULL && currentTCB->sendBufHead->seg.header.seq_num < segPtr->header.seq_num) {
				  		tempSegBuf = currentTCB->sendBufHead;
				    	currentTCB->sendBufHead = currentTCB->sendBufHead->next;
				    	free(tempSegBuf);
				    	printf("Freed seq_num %u\n", tempSegBuf->seg.header.seq_num);
				    	currentTCB->unAck_segNum--;
				  	}

				  	//send if there are unsent segments
				  	if (currentTCB->sendBufunSent != NULL) {
					  	printf("Sending post-DATAACK unsent: %u\n", currentTCB->sendBufunSent->seg.header.seq_num);
					  	pthread_mutex_unlock(currentTCB->bufMutex);
					  	if (sendMaxSegments(currentTCB) < 0) {
							printf("Error sending segments from seghandler.\n");
						}
					} else {
						printf("Nothing unsent for client: %u server: %u.\n", segPtr->header.dest_port, segPtr->header.src_port);
						pthread_mutex_unlock(currentTCB->bufMutex);
					}

				  	/*
				  	while (currentTCB->unAck_segNum < GBN_WINDOW && currentTCB->sendBufunSent != NULL){
				  		struct timespec ts;
				  		currentTCB->sendBufunSent->sentTime = current_utc_time_ns(&ts) / NS_TO_MICROSECONDS;
						if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, &currentTCB->sendBufunSent->seg) < 0) {
							printf("Error sending seg_t with seq_num %u from seghandler.\n", currentTCB->sendBufunSent->seg.header.seq_num);
						} else {
							printf("Sent seg_t with seq_num %u from seghandler.\n", currentTCB->sendBufunSent->seg.header.seq_num);
						}
						currentTCB->unAck_segNum++;
						currentTCB->sendBufunSent = currentTCB->sendBufunSent->next;
				  	}
				  	*/

				  	
				  } else {
				  	printf("Doing nothing.\n");
				  }
				  break;

				case FINWAIT:
				  //printf("State is FINWAIT.\n");
				  if (segPtr->header.type == FINACK  && currentTCB->svr_portNum == segPtr->header.src_port && currentTCB->svr_nodeID==src_nodeID){
				  	printf("Changing state to CLOSED.\n");
				  	currentTCB->state = CLOSED;
				  } else {
				  	printf("Doing nothing.\n");
				  }
				  break;

				default:
				  printf("Unknown state.\n");
				  break;
			}
		} else {
			printf("Couldn't find the client_tcb the server was trying to reach.\n");
		}

		memset(segPtr, 0, sizeof(seg_t));
	
	}

	printf("seghandler is closing the overlay connection.\n");
	//free(client_TCB_Table);
	free(segPtr);
	close(overlay_conn_fd);
	pthread_exit(NULL);
}



// This thread continuously polls send buffer to trigger timeout events
// It should always be running when the send buffer is not empty
// If the current time -  first sent-but-unAcked segment's sent time > DATA_TIMEOUT, a timeout event occurs
// When timeout, resend all sent-but-unAcked segments
// When the send buffer is empty, this thread terminates
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void* sendBuf_timer(void* arg)
{
  	client_tcb_t *currentTCB = (client_tcb_t *)arg;
	struct timespec ts;
	unsigned int currentTime;

	//nanosleep for a bit first
	struct timespec req, rm;
	req.tv_sec = 0;
	req.tv_nsec = BUFTIMER_INIT_SLEEP;
	nanosleep(&req,&rm);

	while (currentTCB->sendBufHead != NULL) {
		currentTime = current_utc_time_ns(&ts) / NS_TO_MICROSECONDS;
		if (currentTime < currentTCB->sendBufHead->sentTime || 
			(currentTime - currentTCB->sendBufHead->sentTime) > DATA_TIMEOUT){

			//send sent-but-unAcked segments again
			pthread_mutex_lock(currentTCB->bufMutex);
			printf("Buftimer timed out!\n");
			int sentSegments = 0;
			segBuf_t *currentSegBuf = currentTCB->sendBufHead;
			while (sentSegments <= currentTCB->unAck_segNum && currentSegBuf != NULL) {

				currentSegBuf->sentTime = current_utc_time_ns(&ts) / NS_TO_MICROSECONDS;
				if (snp_sendseg(overlay_conn_fd, currentTCB->svr_nodeID, &currentSegBuf->seg) < 0) {
					printf("Error sending seg_t with seq_num %u.\n", currentSegBuf->seg.header.seq_num);
				} else {
					printf("Buftimer sent seq_num %u. Client: %u, Server: %u\n", currentSegBuf->seg.header.seq_num, 
				currentSegBuf->seg.header.src_port, currentSegBuf->seg.header.dest_port);
				}
				sentSegments++;
				currentSegBuf = currentSegBuf->next;
			}

			if (currentSegBuf != NULL) {
				currentTCB->sendBufunSent = currentSegBuf;
			} else {
				currentTCB->sendBufunSent = NULL;
			}
			pthread_mutex_unlock(currentTCB->bufMutex);
		} else {
			printf("Not timed out yet. seq_num %u with (current - sentTime) = %lu.\n", 
				currentTCB->sendBufHead->seg.header.seq_num, currentTime - currentTCB->sendBufHead->sentTime);
		}
		req.tv_sec = 0;
		req.tv_nsec = SENDBUF_POLLING_INTERVAL / 2;
		nanosleep(&req,&rm);
	}
	printf("Exiting sendBufTimer for client: %u, server: %u. sendBufHead == NULL: %s\n.", currentTCB->client_portNum, currentTCB->svr_portNum, (currentTCB->sendBufHead == NULL) ? "TRUE" : "FALSE" );
	return NULL;
	pthread_exit(NULL);
}
