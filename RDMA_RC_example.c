/*
* BUILD COMMAND:
* gcc -Wall -I/usr/local/ofed/include -O2 -o RDMA_RC_example -L/usr/local/ofed/lib64 -L/usr/local/ofed/lib -libverbs RDMA_RC_example.c
*
*/
/******************************************************************************
*
* RDMA Aware Networks Programming Example
*
* This code demonstrates how to perform the following operations using the * VPI Verbs API:
*
* Send
* Receive
* RDMA Read
* RDMA Write
*
*****************************************************************************/ 
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
  
/* poll CQ timeout in millisec (2 seconds) */
 
#define MAX_POLL_CQ_TIMEOUT 2000
#define MSG "SEND operation "
#define RDMAMSGR "RDMA read operation "
#define RDMAMSGW "RDMA write operation"
#define MSG_SIZE (strlen(MSG) + 1)

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
 
static inline uint64_t ntohll(uint64_t x) { return x; } 
#else
 
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
 
#endif
 
  
 
/* structure of test parameters */
 
struct config_t
 
{
