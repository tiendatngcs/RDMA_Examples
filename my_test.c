/* 
 * Compile Command:
 * gcc my_test.c -o my_test -libverbs -lrdmacm -pthread
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <rdma/rdma_verbs.h>

#define MESSAGE_LENGTH 10000000

uint16_t getLocalId(struct ibv_context* context, int ib_port) {
  struct ibv_port_attr port_attr;
  ibv_query_port(context, ib_port, &port_attr);
  return port_attr.lid;
}

struct ibv_context* createContext(char* device_name) {
    /* There is no way to directly open the device with its name; we should get the list of devices first. */
    struct ibv_context* context = NULL;
    int num_devices;
    struct ibv_device** device_list = ibv_get_device_list(&num_devices);
    printf("found %d device(s)\n", num_devices);
    for (int i = 0; i < num_devices; i++){
        /* match device name. open the device and return it */
        printf("%s\n", ibv_get_device_name(device_list[i]));
        if (strcmp(device_name, ibv_get_device_name(device_list[i])) == 0) {
            context = ibv_open_device(device_list[i]);
            break;
        }
    }

    /* it is important to free the device list; otherwise memory will be leaked. */
    ibv_free_device_list(device_list);
    if (context == NULL) {
        fprintf(stderr, "Unable to find the device ");
        // std::cerr << "Unable to find the device " << device_name << std::endl;
    }
    return context;
}

struct ibv_qp* createQueuePair(struct ibv_pd* pd, struct ibv_cq* cq) {
    struct ibv_qp_init_attr queue_pair_init_attr;
    memset(&queue_pair_init_attr, 0, sizeof(queue_pair_init_attr));
    queue_pair_init_attr.qp_type = IBV_QPT_RC;
    queue_pair_init_attr.sq_sig_all = 1;       // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
    queue_pair_init_attr.send_cq = cq;         // completion queue can be shared or you can use distinct completion queues.
    queue_pair_init_attr.recv_cq = cq;         // completion queue can be shared or you can use distinct completion queues.
    queue_pair_init_attr.cap.max_send_wr = 8;  // increase if you want to keep more send work requests in the SQ.
    queue_pair_init_attr.cap.max_recv_wr = 8;  // increase if you want to keep more receive work requests in the RQ.
    queue_pair_init_attr.cap.max_send_sge = 8; // increase if you allow send work requests to have multiple scatter gather entry (SGE).
    queue_pair_init_attr.cap.max_recv_sge = 8; // increase if you allow receive work requests to have multiple scatter gather entry (SGE).

    return ibv_create_qp(pd, &queue_pair_init_attr);
}

int changeQueuePairStateToInit(struct ibv_qp* queue_pair) {
    // return 0 if success
    struct ibv_qp_attr init_attr;
    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.qp_state = IBV_QPS_INIT;
    init_attr.port_num = 1; // Use the first port
    init_attr.pkey_index = 0;
    init_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

    return ibv_modify_qp(queue_pair, &init_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
}

int changeQueuePairStateToRTR(struct ibv_qp* queue_pair, int ib_port, uint32_t destination_qp_number, uint16_t destination_local_id) {
  struct ibv_qp_attr rtr_attr;
  memset(&rtr_attr, 0, sizeof(rtr_attr));
  rtr_attr.qp_state = IBV_QPS_RTR;
  rtr_attr.path_mtu = IBV_MTU_1024;
  rtr_attr.rq_psn = 0;
  rtr_attr.max_dest_rd_atomic = 1;
  rtr_attr.min_rnr_timer = 0x12;
  rtr_attr.ah_attr.is_global = 0;
  rtr_attr.ah_attr.sl = 0;
  rtr_attr.ah_attr.src_path_bits = 0;
  rtr_attr.ah_attr.port_num = ib_port;
  
  rtr_attr.dest_qp_num = destination_qp_number;
  rtr_attr.ah_attr.dlid = destination_local_id;

  return ibv_modify_qp(queue_pair, &rtr_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
}

int main (int argc, char** argv) {
    int ret;

    struct ibv_context* context = createContext("mlx5_0");
    struct ibv_pd* protection_domain = ibv_alloc_pd(context);

    // Create Completion Queue
    int cq_size = 0x10;
    struct ibv_cq* completion_queue = ibv_create_cq(context, cq_size, NULL, NULL, 0);

    // Create Queue Pair using the created protection domain and completion queue
    struct ibv_qp* queue_pair = createQueuePair(protection_domain, completion_queue);
    printf("State of qp %d\n", queue_pair->state);

    ret = changeQueuePairStateToInit(queue_pair);
    printf("Queue Pair changed to Init state: %s\n", strerror(ret));
    printf("State of qp %d\n", queue_pair->state);
    printf("Queue Pair num: %d\n", queue_pair->qp_num);
    printf("Queue Pair local ID: %d\n", getLocalId(context, 1));

    // ret = changeQueuePairStateToRTR(queue_pair, 1, queue_pair->qp_num, );
    ibv_destroy_qp(queue_pair);
    rdma_post_write
}