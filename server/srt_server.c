// FILE: srt_server.c
//
// Description: this file contains server states' definition, some important
// data structures and the server SRT socket interface definitions. You need 
// to implement all these interfaces
//
// Date: April 18, 2008
//       April 21, 2008 **Added more detailed description of prototypes fixed ambiguities** ATC
//       April 26, 2008 **Added GBN descriptions
//
#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "srt_server.h"
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
svr_tcb_t *server_TCB_Table[MAX_TRANSPORT_CONNECTIONS];


//
//
//  SRT socket API for the server side application. 
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
// handle the incoming segments. There is only one seghandler for the server side which
// handles call connections for the client.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void srt_server_init(int conn)
{
	// instantiation of TCB table
	//server_TCB_Table = malloc(sizeof(svr_tcb_t *) * MAX_TRANSPORT_CONNECTIONS);
	//MALLOC_CHECK(server_TCB_Table);
	//memset(server_TCB_Table, 0, sizeof(svr_tcb_t *) * MAX_TRANSPORT_CONNECTIONS);

	//Initialize global overaly TCP socket descriptor for sendseg and recvseg
	overlay_conn_fd = conn;

	// instantiation of seghandler thread
	//there's only one seghandler for the server side. Start seghandler thread to handle incoming segments
	pthread_t segHandlerThread;
    if (pthread_create(&segHandlerThread, NULL, seghandler, NULL)){
    	printf("Error creating seghandler thread.\n");
    }

	printf("Initialized server.\n");
}


// This function looks up the client TCB table to find the first NULL entry, and creates
// a new TCB entry using malloc() for that entry. All fields in the TCB are initialized 
// e.g., TCB state is set to CLOSED and the server port set to the function call parameter 
// server port.  The TCB table entry index should be returned as the new socket ID to the server 
// and be used to identify the connection on the server side. If no entry in the TCB table  
// is available the function returns -1.

//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_sock(unsigned int port)
{
  //malloc new TCB entry at first NULL entry
	int sockfd = 0;
	while (sockfd < MAX_TRANSPORT_CONNECTIONS){
		if (server_TCB_Table[sockfd] == NULL) {

			//malloc new client tcb entry
			server_TCB_Table[sockfd] = malloc(sizeof(svr_tcb_t));
			MALLOC_CHECK(server_TCB_Table[sockfd]);
			memset(server_TCB_Table[sockfd], 0, sizeof(svr_tcb_t));

			//initialize TCB entry, malloc recv buffer and mutex
			server_TCB_Table[sockfd]->svr_portNum = port;
			server_TCB_Table[sockfd]->state = CLOSED;
			server_TCB_Table[sockfd]->usedBufLen = 0;
			server_TCB_Table[sockfd]->expect_seqNum = 0;
			server_TCB_Table[sockfd]->recvBuf = malloc(RECEIVE_BUF_SIZE);
			memset(server_TCB_Table[sockfd]->recvBuf, 0, RECEIVE_BUF_SIZE);
			server_TCB_Table[sockfd]->bufMutex = malloc(sizeof(pthread_mutex_t));
			memset(server_TCB_Table[sockfd]->bufMutex, 0, sizeof(pthread_mutex_t));
			server_TCB_Table[sockfd]->svr_nodeID = topology_getMyNodeID();
			printf("My nodeID is %u.\n", server_TCB_Table[sockfd]->svr_nodeID);

			//initialize mutex
			if (pthread_mutex_init(server_TCB_Table[sockfd]->bufMutex, NULL) != 0) {
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
		printf("Created new TCB server entry with sockfd %d.\n", sockfd);
		return sockfd;
	}
}


// This function gets the TCB pointer using the sockfd and changes the state of the connection to 
// LISTENING. It then starts a timer to ``busy wait'' until the TCB's state changes to CONNECTED 
// (seghandler does this when a SYN is received). It waits in an infinite loop for the state 
// transition before proceeding and to return 1 when the state change happens, dropping out of
// the busy wait loop. You can implement this blocking wait in different ways, if you wish.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_accept(int sockfd)
{
  //find TCB entry
	svr_tcb_t *currentTCB = server_TCB_Table[sockfd];
	if (currentTCB == NULL){
		printf("Couldn't find the specified server TCB entry.\n");
		return -1;
	}

	switch(currentTCB->state) {
		case CLOSED:
		  printf("State is CLOSED. Transitioning to LISTENING.\n");
		  break;

		case LISTENING:
		  printf("State is LISTENING. Can't accept.\n");
		  return -1;

		case CONNECTED:
		  printf("State is CONNECTED. Can't accept.\n");
		  return -1;

		case CLOSEWAIT:
		  printf("State is CLOSEWAIT. Can't accept.\n");
		  return -1;

		default:
		  printf("Unknown state. Can't accept.\n");
		  return -1;
	}	

	currentTCB->state = LISTENING;

	while (currentTCB->state == LISTENING) {
		struct timespec req;
		req.tv_sec = 0;
		req.tv_nsec = ACCEPT_POLLING_INTERVAL;
		struct timespec rm;
		nanosleep(&req,&rm);
	}

	//if SYN is received, seghandler will change state to CONNECTED
	if (currentTCB->state == CONNECTED) {
		printf("We're CONNECTED!\n");
		return 1;

	} else {
		printf("Couldn't connect. Switching to CLOSED.\n");
		currentTCB->state = CLOSED;
		return -1;
	}
}


// Receive data from a srt client. Recall this is a unidirectional transport
// where DATA flows from the client to the server. Signaling/control messages
// such as SYN, SYNACK, etc.flow in both directions. 
// This function keeps polling the receive buffer every RECVBUF_POLLING_INTERVAL
// until the requested data is available, then it stores the data and returns 1
// If the function fails, return -1 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_recv(int sockfd, void* buf, unsigned int length)
{
	//find TCB entry
	svr_tcb_t *currentTCB = server_TCB_Table[sockfd];
	if (currentTCB == NULL){
		printf("Couldn't find the specified server TCB entry in srt_server_recv.\n");
		return -1;
	}

	if (length > RECEIVE_BUF_SIZE) {
		printf("Error: length > RECEIVE_BUF_SIZE.\n");
		return -1;
	}

	//wait until all of the data has been transmitted
	while (currentTCB->usedBufLen < length) {
		struct timespec req;
		req.tv_sec = 0;
		req.tv_nsec = RECVBUF_POLLING_INTERVAL_NS;
		struct timespec rm;
		nanosleep(&req,&rm);
	}

	//return the data
	pthread_mutex_lock(currentTCB->bufMutex);

	//first copy data from 0 to length
	currentTCB->recvBuf -= currentTCB->usedBufLen;
	memcpy(buf, currentTCB->recvBuf, length);
	printf("usedBufLen(%u) >= length (%u). Returning data (%s).\n", currentTCB->usedBufLen, length, buf);
	//printf("Recv Buffer: %s, Buf: %s\n", currentTCB->recvBuf, buf);

	//move data between length and usedBufLen to 0
	char *tempPtr = currentTCB->recvBuf + length;
	currentTCB->usedBufLen = currentTCB->usedBufLen - length;
	memmove(currentTCB->recvBuf, tempPtr, currentTCB->usedBufLen);

	//memset
	tempPtr = currentTCB->recvBuf + currentTCB->usedBufLen;
	memset(tempPtr, 0, RECEIVE_BUF_SIZE - currentTCB->usedBufLen);
	//printf("usedBufLen is now %u, Recv Buffer: %s.\n", currentTCB->usedBufLen, currentTCB->recvBuf);
	currentTCB->recvBuf += currentTCB->usedBufLen;

	pthread_mutex_unlock(currentTCB->bufMutex);
	//printf("Returning from srt_server_recv with length %u\n", length);
	return 1;
}


// This function calls free() to free the TCB entry. It marks that entry in TCB as NULL
// and returns 1 if succeeded (i.e., was in the right state to complete a close) and -1 
// if fails (i.e., in the wrong state).
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_close(int sockfd)
{
  	//find TCB entry
	svr_tcb_t *currentTCB = server_TCB_Table[sockfd];
	if (currentTCB == NULL){
		printf("Couldn't find the specified server TCB entry.\n");
		return -1;
	}

	switch(currentTCB->state) {
		case CLOSED:
		  printf("State is CLOSED. Freeing TCB entry and closing.\n");
		  printf("Destroying mutex.\n");
		  pthread_mutex_lock(currentTCB->bufMutex);
		  currentTCB->recvBuf -= currentTCB->usedBufLen;
		  currentTCB->usedBufLen = 0;
		  pthread_mutex_unlock(currentTCB->bufMutex);
		  pthread_mutex_destroy(server_TCB_Table[sockfd]->bufMutex);
		  free(server_TCB_Table[sockfd]->bufMutex);
		  printf("Freeing recv buffer.\n");
		  free(server_TCB_Table[sockfd]->recvBuf);
		  printf("Freeing server_TCB_Table[sockfd].\n");
		  free(server_TCB_Table[sockfd]);
		  server_TCB_Table[sockfd] = NULL;
		  return 1;

		case LISTENING:
		  printf("State is LISTENING. Can't close.\n");
		  return -1;

		case CONNECTED:
		  printf("State is CONNECTED. Can't close.\n");
		  return -1;

		case CLOSEWAIT:
		  printf("State is CLOSEWAIT. Can't close.\n");
		  return -1;

		default:
		  printf("Unknown state. Can't close.\n");
		  return -1;
	}
}


// This is a thread  started by srt_server_init(). It handles all the incoming 
// segments from the client. The design of seghanlder is an infinite loop that calls snp_recvseg(). If
// snp_recvseg() fails then the overlay connection is closed and the thread is terminated. Depending
// on the state of the connection when a segment is received  (based on the incoming segment) various
// actions are taken. See the client FSM for more details.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void* seghandler(void* arg)
{
  	seg_t *segPtr = malloc(sizeof(seg_t));
	MALLOC_CHECK(segPtr);
	memset(segPtr, 0, sizeof(seg_t));

	char *segTypeStrings[] = {"SYN", "SYNACK", "FIN", "FINACK", "DATA", "DATAACK"};
	char *states[] = {"Unknown", "CLOSED", "LISTENING", "CONNECTED", "CLOSEWAIT"};
	int src_nodeID;

	while (snp_recvseg(overlay_conn_fd, &src_nodeID, segPtr) > 0) {

		if(!segPtr)
			break;

		//get the right server_tcb_t index
		unsigned int destPort = segPtr->header.dest_port;
		int idx = 0;
		while (idx < MAX_TRANSPORT_CONNECTIONS) {
			if (server_TCB_Table[idx] != NULL) {
				if (server_TCB_Table[idx]->svr_portNum == destPort){
					break;
				}
			}
			idx++;
		}

		if (idx < MAX_TRANSPORT_CONNECTIONS){
			svr_tcb_t *currentTCB = server_TCB_Table[idx];
			printf("\nReceived %s in state %s. client: %u, server: %u.\n", 
				segTypeStrings[segPtr->header.type], states[currentTCB->state], segPtr->header.src_port, segPtr->header.dest_port);

			switch(currentTCB->state) {
				case CLOSED:
				  //printf("State is CLOSED.\n");
				  printf("Doing nothing.\n");
				  break;

				case LISTENING:
				  //printf("State is LISTENING.\n");
				  if (segPtr->header.type == SYN){
				  	printf("Changing state to CONNECTED. client_portNum: %u, expect_seqNum: %u. Sending SYNACK.\n",
				  		 segPtr->header.src_port, segPtr->header.seq_num);
				  	currentTCB->state = CONNECTED;
				  	currentTCB->client_portNum = segPtr->header.src_port;
					currentTCB->expect_seqNum = segPtr->header.seq_num;
					currentTCB->client_nodeID = src_nodeID; //new
				  	
				  	//create SYNACK seg_t
					seg_t* synSegPtr = malloc(sizeof(seg_t));
					MALLOC_CHECK(synSegPtr);
					memset(synSegPtr, 0, sizeof(seg_t));
					synSegPtr->header.src_port = currentTCB->svr_portNum;
					synSegPtr->header.dest_port = currentTCB->client_portNum;
					synSegPtr->header.type = SYNACK;

					//send SYNACK seg_t
					if (snp_sendseg(overlay_conn_fd, currentTCB->client_nodeID, synSegPtr) < 0) {
						printf("Error sending SYNACK seg_t.\n");
					}
					free(synSegPtr);
				  } else {
				  	printf("Doing nothing.\n");
				  }
				  break;

				case CONNECTED:
				  //printf("State is CONNECTED.\n");
				  if (segPtr->header.type == SYN  && currentTCB->client_portNum == segPtr->header.src_port && currentTCB->client_nodeID == src_nodeID){
				  	printf("Sending SYNACK.\n");

				  	//create SYNACK seg_t
					seg_t* synSegPtr = malloc(sizeof(seg_t));
					MALLOC_CHECK(synSegPtr);
					memset(synSegPtr, 0, sizeof(seg_t));
					synSegPtr->header.src_port = currentTCB->svr_portNum;
					synSegPtr->header.dest_port = currentTCB->client_portNum;
					synSegPtr->header.type = SYNACK;

					//send SYNACK seg_t
					if (snp_sendseg(overlay_conn_fd, currentTCB->client_nodeID, synSegPtr) < 0) {
						printf("Error sending SYNACK seg_t.\n");
					}
					free(synSegPtr);
				  } else if (segPtr->header.type == FIN  && currentTCB->client_portNum == segPtr->header.src_port && currentTCB->client_nodeID == src_nodeID) {

				  	printf("Changing state to CLOSEWAIT and sending FINACK.\n");
				  	currentTCB->state = CLOSEWAIT;
				  	pthread_t closeWaitThread;
				    if (pthread_create(&closeWaitThread, NULL, closeWaitTimer, currentTCB)){
				    	printf("Error creating closeWaitTimer thread.\n");
				    }

				  	//create FINACK seg_t
					seg_t *finSegPtr = malloc(sizeof(seg_t));
					MALLOC_CHECK(finSegPtr);
					memset(finSegPtr, 0, sizeof(seg_t));
					finSegPtr->header.src_port = currentTCB->svr_portNum;
					finSegPtr->header.dest_port = currentTCB->client_portNum;
					finSegPtr->header.type = FINACK;

					//send FINACK seg_t
					if (snp_sendseg(overlay_conn_fd, currentTCB->client_nodeID, finSegPtr) < 0) {
						printf("Error sending FINACK seg_t.\n");
					}
					free(finSegPtr);

				  } else if (segPtr->header.type == DATA  && currentTCB->client_portNum == segPtr->header.src_port && currentTCB->client_nodeID == src_nodeID) {

					  	//create DATAACK seg_t
						seg_t *dataSegPtr = malloc(sizeof(seg_t));
						MALLOC_CHECK(dataSegPtr);
						memset(dataSegPtr, 0, sizeof(seg_t));
						dataSegPtr->header.src_port = currentTCB->svr_portNum;
						dataSegPtr->header.dest_port = currentTCB->client_portNum;
						dataSegPtr->header.type = DATAACK;

						//if the seq_nums match, add to buffer and increment relevant variables
						pthread_mutex_lock(currentTCB->bufMutex);
						if (segPtr->header.seq_num == currentTCB->expect_seqNum) {
							//put received data in recv buffer if it can fit
							if (segPtr->header.length + currentTCB->usedBufLen < RECEIVE_BUF_SIZE) {
								printf("Seq_nums match (%u)! Adding to buffer.\n", segPtr->header.seq_num);
								memcpy(currentTCB->recvBuf, segPtr->data, segPtr->header.length);
								currentTCB->recvBuf += segPtr->header.length;
								currentTCB->usedBufLen += segPtr->header.length;
								currentTCB->expect_seqNum += segPtr->header.length;
							} else {
								printf("Seq_nums match but recv Buf is too full. Dropping data and sending DATAACK.\n");
							}
						} else {
							printf("Out of order packet (%u).\n", segPtr->header.seq_num);
						}
						dataSegPtr->header.seq_num = currentTCB->expect_seqNum;
						pthread_mutex_unlock(currentTCB->bufMutex);

						//send DATAACK seg_t
						printf("Sending DATAACK with expect_seqNum %u.\n", dataSegPtr->header.seq_num);
						if (snp_sendseg(overlay_conn_fd, currentTCB->client_nodeID, dataSegPtr) < 0) {
							printf("Error sending DATAACK seg_t.\n");
						}
						free(dataSegPtr);
				  } else {
				  	printf("Doing nothing.\n");
				  }
				  break;

				case CLOSEWAIT:
				  //printf("State is CLOSEWAIT.\n");
				  if (segPtr->header.type == FIN  && currentTCB->client_portNum == segPtr->header.src_port && currentTCB->client_nodeID == src_nodeID){

				  	printf("Sending FINACK.\n");
				  	//create FINACK seg_t
					seg_t* synSegPtr = malloc(sizeof(seg_t));
					MALLOC_CHECK(synSegPtr);
					memset(synSegPtr, 0, sizeof(seg_t));
					synSegPtr->header.src_port = currentTCB->svr_portNum;
					synSegPtr->header.dest_port = currentTCB->client_portNum;
					synSegPtr->header.type = FINACK;

					//send FINACK seg_t
					if (snp_sendseg(overlay_conn_fd, currentTCB->client_nodeID, synSegPtr) < 0) {
						printf("Error sending FINACK seg_t.\n");
					}
					free(synSegPtr);

				  } else {
				  	printf("Doing nothing.\n");
				  }
				  break;

				default:
				  printf("Unknown state.\n");
				  break;
			}
		} else {
			printf("Couldn't find the server_tcb the client was trying to reach.\n");
		}

		memset(segPtr, 0, sizeof(seg_t));
	
	}

	printf("seghandler is closing the overlay connection.\n");
	close(overlay_conn_fd);
	free(segPtr);
	pthread_exit(NULL);
}



//after a set amount of time, switches currentTCB's state to CLOSED
void *closeWaitTimer(void* arg){
	svr_tcb_t *currentTCB = (svr_tcb_t *)arg;

	//start timer
	struct timespec ts;
	unsigned long  tstart, tend;
	tstart = current_utc_time_ns(&ts);

	while (currentTCB->state == CLOSEWAIT) {
		//calc time elapsed
		tend = current_utc_time_ns(&ts);
		
		//if timeout, change state to CLOSED and clear recv Buffer
		//printf("tend - tstart = %ld\n", labs(tend-tstart));
		if (tend - tstart > NANOSECONDS_PER_SECOND) {
			currentTCB->state = CLOSED;
			break;
		}
	}
	printf("CLOSEWAIT time up! Changing state to CLOSED.\n");
	pthread_exit(NULL);
}




