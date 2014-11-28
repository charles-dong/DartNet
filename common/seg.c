
#include "seg.h"
#include "stdio.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>

//SRT process uses this function to send a segment and its destination node ID in a sendseg_arg_t structure
// to SNP process to send out. 
//Parameter network_conn is the TCP descriptor of the connection between the SRT process and the SNP process. 
//Return 1 if a sendseg_arg_t is succefully sent, otherwise return -1.
int snp_sendseg(int network_conn, int dest_nodeID, seg_t* segPtr)
{
	//set checksum
  	segPtr->header.checksum = checksum(segPtr);

  	//package into sendseg_arg_t
  	sendseg_arg_t temp;
  	temp.nodeID = dest_nodeID;
  	temp.seg = *segPtr;

  	//send to SNP on network conn
	char bufstart[2];
	char bufend[2];
	bufstart[0] = '!';
	bufstart[1] = '&';
	bufend[0] = '!';
	bufend[1] = '#';
	if (send(network_conn, bufstart, 2, 0) < 0) {
		return -1;
	}
	if(send(network_conn,&temp,sizeof(sendseg_arg_t),0)<0) {
		return -1;
	}
	if(send(network_conn,bufend,2,0)<0) {
		return -1;
	}
	return 1;
}

//SRT process uses this function to receive a  sendseg_arg_t structure which contains a segment and its 
// src node ID from the SNP process. 
//Parameter network_conn is the TCP descriptor of the connection between the SRT process and the SNP process. 
//When a segment is received, use seglost to determine if the segment should be discarded, also check the checksum.  
//Return 1 if a sendseg_arg_t is succefully received, otherwise return -1.
int snp_recvseg(int network_conn, int* src_nodeID, seg_t* segPtr)
{
	char buf[sizeof(sendseg_arg_t)+2];
	char c;
	int idx = 0;
	// state can be 0,1,2,3; 
	// 0 starting point 
	// 1 '!' received
	// 2 '&' received, start receiving segment
	// 3 '!' received,
	// 4 '#' received, finish receiving segment 
	int state = 0;
	while(recv(network_conn,&c,1,0)>0) {
		if (state == 0) {
		        if(c=='!')
				state = 1;
		}
		else if(state == 1) {
			if(c=='&') 
				state = 2;
			else
				state = 0;
		}
		else if(state == 2) {
			if(c=='!') {
				buf[idx]=c;
				idx++;
				state = 3;
			}
			else {
				buf[idx]=c;
				idx++;
			}
		}
		else if(state == 3) {
			if(c=='#') {
				buf[idx]=c;
				idx++;
				state = 0;
				idx = 0;
				sendseg_arg_t tempSendSegArgT;
				memcpy(&tempSendSegArgT,buf,sizeof(sendseg_arg_t));
				seg_t tempSeg = tempSendSegArgT.seg;

				if(seglost(&tempSeg) > 0) {
                	continue;
                } else {
                	if (checkchecksum(&tempSeg) < 0) {
                		printf("Checksum failed! Dropping packet.\n");
                		continue;
                	}
                }
                memcpy(src_nodeID, &tempSendSegArgT.nodeID, sizeof(int));
				memcpy(segPtr,&tempSeg,sizeof(seg_t));
				return 1;
			}
			else if(c=='!') {
				buf[idx]=c;
				idx++;
			}
			else {
				buf[idx]=c;
				idx++;
				state = 2;
			}
		}
	}
	return -1;
}

//SNP process uses this function to receive a sendseg_arg_t structure which contains a segment and its
// destination node ID from the SRT process.
//Parameter tran_conn is the TCP descriptor of the connection between the SRT process and the SNP process. 
//Return 1 if a sendseg_arg_t is succefully received, otherwise return -1.
int getsegToSend(int tran_conn, int* dest_nodeID, seg_t* segPtr)
{
	char buf[sizeof(sendseg_arg_t)+2];
	char c;
	int idx = 0;
	// state can be 0,1,2,3; 
	// 0 starting point 
	// 1 '!' received
	// 2 '&' received, start receiving segment
	// 3 '!' received,
	// 4 '#' received, finish receiving segment 
	int state = 0;
	while(recv(tran_conn,&c,1,0)>0) {
		if (state == 0) {
		        if(c=='!')
				state = 1;
		}
		else if(state == 1) {
			if(c=='&') 
				state = 2;
			else
				state = 0;
		}
		else if(state == 2) {
			if(c=='!') {
				buf[idx]=c;
				idx++;
				state = 3;
			}
			else {
				buf[idx]=c;
				idx++;
			}
		}
		else if(state == 3) {
			if(c=='#') {
				buf[idx]=c;
				idx++;
				state = 0;
				idx = 0;
				sendseg_arg_t tempSendSegArgT;
				memcpy(&tempSendSegArgT,buf,sizeof(sendseg_arg_t));
				seg_t tempSeg = tempSendSegArgT.seg;
                memcpy(dest_nodeID, &tempSendSegArgT.nodeID, sizeof(int));
				memcpy(segPtr,&tempSeg,sizeof(seg_t));
				return 1;
			}
			else if(c=='!') {
				buf[idx]=c;
				idx++;
			}
			else {
				buf[idx]=c;
				idx++;
				state = 2;
			}
		}
	}
	return -1;
}

//SNP process uses this function to send a sendseg_arg_t structure which contains a segment and 
// its src node ID to the SRT process.
//Parameter tran_conn is the TCP descriptor of the connection between the SRT process and the SNP process. 
//Return 1 if a sendseg_arg_t is succefully sent, otherwise return -1.
int forwardsegToSRT(int tran_conn, int src_nodeID, seg_t* segPtr)
{
	//package into sendseg_arg_t
  	sendseg_arg_t temp;
  	temp.nodeID = src_nodeID;
  	temp.seg = *segPtr;

  	//send to SNP on network conn
	char bufstart[2];
	char bufend[2];
	bufstart[0] = '!';
	bufstart[1] = '&';
	bufend[0] = '!';
	bufend[1] = '#';
	if (send(tran_conn, bufstart, 2, 0) < 0) {
		return -1;
	}
	if(send(tran_conn,&temp,sizeof(sendseg_arg_t),0)<0) {
		return -1;
	}
	if(send(tran_conn,bufend,2,0)<0) {
		return -1;
	}
	return 1;
}

// for seglost(seg_t* segment):
// a segment has PKT_LOST_RATE probability to be lost or invalid checksum
// with PKT_LOST_RATE/2 probability, the segment is lost, this function returns 1
// If the segment is not lost, return 0. 
// Even the segment is not lost, the packet has PKT_LOST_RATE/2 probability to have invalid checksum
// We flip  a random bit in the segment to create invalid checksum
int seglost(seg_t* segPtr) {
	int random = rand()%100;
	if(random<PKT_LOSS_RATE*100) {
		if(rand()%2==0) {
			printf("Segment lost!\n");
      	return 1;
		} else {
			//get data length
			int len = sizeof(srt_hdr_t)+segPtr->header.length;
			//get a random bit that will be flipped
			int errorbit = rand()%(len*8);
			//flip the bit
			char* temp = (char*)segPtr;
			temp = temp + errorbit/8;
			*temp = *temp^(1<<(errorbit%8));
			printf("Creating invalid checksum!!!\n");
			return 0;
		}
	}
	return 0;

}
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//This function calculates checksum over the given segment.
//The checksum is calculated over the segment header and segment data.
//You should first clear the checksum field in segment header to be 0.
//If the data has odd number of octets, add an 0 octets to calculate checksum.
//Use 1s complement for checksum calculation.
unsigned short checksum(seg_t* segment)
{
	segment->header.checksum = 0;
	int count;
	if (segment->header.type == DATA) {
		count = sizeof(seg_t);
	} else {
		count = sizeof(srt_hdr_t);
	}
	/* Compute Internet Checksum for "count" bytes
	*         beginning at location "addr". From the RFC the assignment page linked to
	*/
	unsigned short *buf = (unsigned short *)segment;
	unsigned long sum = 0;

	while( count > 1 )  {
	   /*  This is the inner loop */
	       sum += *(buf++);
	       count -= 2;
	}

	   /*  Add left-over byte, if any */
	if( count > 0 )
	       sum += *(unsigned int *)buf;

	   /*  Fold 32-bit sum to 16 bits */
	   sum = (sum & 0xFFFF) + (sum >> 16);
	   sum += sum >> 16;
	
	//printf("checksum: %hu for data %s\n", (unsigned short)~sum, segment->data);
	return (unsigned short)~sum;
}

//Check the checksum in the segment,
//return 1 if the checksum is valid,
//return -1 if the checksum is invalid
int checkchecksum(seg_t* segment)
{
		int count;
	if (segment->header.type == DATA) {
		count = sizeof(seg_t);
	} else {
		count = sizeof(srt_hdr_t);
	}
	/* Compute Internet Checksum for "count" bytes
	*         beginning at location "addr". From the RFC the assignment page linked to
	*/
	unsigned short *buf = (unsigned short *)segment;
	unsigned long sum = 0;

	while( count > 1 )  {
	   /*  This is the inner loop */
	       sum += *(buf++);
	       count -= 2;
	}

	   /*  Add left-over byte, if any */
	if( count > 0 )
	       sum += *(unsigned int *)buf<<8;

	   /*  Fold 32-bit sum to 16 bits */
	   sum = (sum & 0xFFFF) + (sum >> 16);
	   sum += sum >> 16;
	printf("\nChecksum: %s for data: %s\n",((unsigned short)~sum == 0) ? "valid" : "NOT valid", segment->data);
	if ((unsigned short)~sum == 0){
		return 1;
	} else {
		return -1;
	}
}
