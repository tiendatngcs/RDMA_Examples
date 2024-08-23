#include <infiniband/verbs.h>
#include <stdio.h>

// Follow this official user guide
// https://docs.nvidia.com/rdma-aware-networks-programming-user-manual-1-7.pdf
// https://docs.nvidia.com/networking/display/rdmaawareprogrammingv17/programming+examples+using+ibv+verbs#
// And this blog post
// https://insujang.github.io/2020-02-09/introduction-to-programming-infiniband/

uint8_t PORT_NUM = 7471;
#define SERVER_ADDR "192.168.0.21"


struct ibv_context * ib_ctx_ = NULL;

// first we open a device
int open_mlx_device(const char *device_name) {
    int num_devices = 0;
    ibv_device **device_list = ibv_get_device_list(&num_devices);
    if (!device_list || num_devices <= 0) {
        return -1;
    }
    if (strlen(device_name) == 0) {
#ifdef USE_MLX5DV
        assert(mlx5dv_is_supported(device_list[0]));
#endif
        ib_ctx_ = ibv_open_device(device_list[0]);
        ibv_free_device_list(device_list);
        printf("No device");
        return ib_ctx_ ? 0 : -1;
    } else {
        printf("get device %s num device %d\n", device_name, num_devices);
        for (int i = 0; i < num_devices; ++i) {
            const char *target_device_name = ibv_get_device_name(device_list[i]);
            if (target_device_name && strcmp(target_device_name, device_name) == 0) {
#ifdef USE_MLX5DV
                assert(mlx5dv_is_supported(device_list[i]));
#endif
                ib_ctx_ = ibv_open_device(device_list[i]);
                if (ib_ctx_) {
                    ibv_free_device_list(device_list);
                    return 0;
                }
            }
        }
    }
    ibv_free_device_list(device_list);
    return -1;
}

// 2. create a context domain
// 2.1 create context
// 2.2 create context domain
struct ibv_context* createContext(const char *device_name) {
  /* There is no way to directly open the device with its name; we should get the list of devices first. */
  // essentially it is just open device again, this time return device context.
  struct ibv_context* context = nullptr;
  int num_devices;
  struct ibv_device** device_list = ibv_get_device_list(&num_devices);
  for (int i = 0; i < num_devices; i++){
    /* match device name. open the device and return it */
    const char *target_device_name = ibv_get_device_name(device_list[i]);
    if (target_device_name && strcmp(target_device_name, device_name) == 0) {
      context = ibv_open_device(device_list[i]);
      break;
    }
  }

  /* it is important to free the device list; otherwise memory will be leaked. */
  ibv_free_device_list(device_list);
  if (context == nullptr) {
    printf("Unable to find the device \n");
  }
  return context;
}

// 4. Create a queue pair
struct ibv_qp* createQueuePair(struct ibv_pd* pd, struct ibv_cq* cq) {
  struct ibv_qp_init_attr queue_pair_init_attr;
  memset(&queue_pair_init_attr, 0, sizeof(queue_pair_init_attr));
  queue_pair_init_attr.qp_type = IBV_QPT_RC;
  queue_pair_init_attr.sq_sig_all = 1;       // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
  queue_pair_init_attr.send_cq = cq;         // completion queue can be shared or you can use distinct completion queues.
  queue_pair_init_attr.recv_cq = cq;         // completion queue can be shared or you can use distinct completion queues.

  // This is the number of outstanding work requests | max_qp_wr is 32768 for this device
  queue_pair_init_attr.cap.max_send_wr = 248;  // increase if you want to keep more send work requests in the SQ.
  queue_pair_init_attr.cap.max_recv_wr = 248;  // increase if you want to keep more receive work requests in the RQ.

  // scatter-gather allows read/write to multiple remote destination with a single RDMA call. The limit is 16.  
  queue_pair_init_attr.cap.max_send_sge = 1; // increase if you allow send work requests to have multiple scatter gather entry (SGE).
  queue_pair_init_attr.cap.max_recv_sge = 1; // increase if you allow receive work requests to have multiple scatter gather entry (SGE).

  return ibv_create_qp(pd, &queue_pair_init_attr);
}

bool changeQueuePairStateToInit(struct ibv_qp* queue_pair) {
  struct ibv_qp_attr init_attr;
  memset(&init_attr, 0, sizeof(init_attr));
  init_attr.qp_state = ibv_qp_state::IBV_QPS_INIT;
  init_attr.port_num = 1; // local IB port to work with. 
  init_attr.pkey_index = 0;
  init_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  return ibv_modify_qp(queue_pair, &init_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) == 0 ? true : false;
}

// static int post_receive(struct resources *res)
// {
// struct ibv_recv_wr rr;
// struct ibv_sge sge;
// struct ibv_recv_wr *bad_wr;
// int rc;
// /* prepare the scatter/gather entry */
// memset(&sge, 0, sizeof(sge));
// sge.addr = (uintptr_t)res->buf;
// sge.length = MSG_SIZE;
// sge.lkey = res->mr->lkey;
// /* prepare the receive work request */
// memset(&rr, 0, sizeof(rr));
// rr.next = NULL;
// rr.wr_id = 0;
// rr.sg_list = &sge;
// rr.num_sge = 1;
// /* post the Receive Request to the RQ */
// rc = ibv_post_recv(res->qp, &rr, &bad_wr);
// if (rc)
// fprintf(stderr, "failed to post RR\n");
// else
// fprintf(stdout, "Receive Request was posted\n");
// return rc;
// }

bool changeQueuePairStateToRTR(struct ibv_qp* queue_pair, int ib_port, uint32_t destination_qp_number, uint16_t destination_local_id) {
  struct ibv_qp_attr rtr_attr;
  memset(&rtr_attr, 0, sizeof(rtr_attr));
  rtr_attr.qp_state = ibv_qp_state::IBV_QPS_RTR;
  rtr_attr.path_mtu = ibv_mtu::IBV_MTU_1024;
  rtr_attr.rq_psn = 0;
  rtr_attr.max_dest_rd_atomic = 1;
  rtr_attr.min_rnr_timer = 0x12;
  rtr_attr.ah_attr.is_global = 0;
  rtr_attr.ah_attr.sl = 0;
  rtr_attr.ah_attr.src_path_bits = 0;
  rtr_attr.ah_attr.port_num = ib_port;
  
  rtr_attr.dest_qp_num = destination_qp_number;
  rtr_attr.ah_attr.dlid = destination_local_id;

  return ibv_modify_qp(queue_pair, &rtr_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) == 0 ? true : false;
}

static int connect_qp(struct resources *res) {
  struct cm_con_data_t local_con_data;
  struct cm_con_data_t remote_con_data;
  struct cm_con_data_t tmp_con_data;
  int rc = 0;
  char temp_char;
  union ibv_gid my_gid;
  if (config.gid_idx >= 0) {
    rc = ibv_query_gid(res->ib_ctx, config.ib_port, config.gid_idx, &my_gid);
    if (rc) {
      122 fprintf(stderr, "could not get gid for port %d, index %d\n",
                  config.ib_port, config.gid_idx);
      return rc;
    }
  } else
    memset(&my_gid, 0, sizeof my_gid);
  /* exchange using TCP sockets info required to connect QPs */
  local_con_data.addr = htonll((uintptr_t)res->buf);
  local_con_data.rkey = htonl(res->mr->rkey);
  local_con_data.qp_num = htonl(res->qp->qp_num);
  local_con_data.lid = htons(res->port_attr.lid);
  memcpy(local_con_data.gid, &my_gid, 16);
  fprintf(stdout, "\nLocal LID = 0x%x\n", res->port_attr.lid);
  if (sock_sync_data(res->sock, sizeof(struct cm_con_data_t),
                     (char *)&local_con_data, (char *)&tmp_con_data) < 0) {
    fprintf(stderr, "failed to exchange connection data between sides\n");
    rc = 1;
    goto connect_qp_exit;
  }
  remote_con_data.addr = ntohll(tmp_con_data.addr);
  remote_con_data.rkey = ntohl(tmp_con_data.rkey);
  remote_con_data.qp_num = ntohl(tmp_con_data.qp_num);
  remote_con_data.lid = ntohs(tmp_con_data.lid);
  memcpy(remote_con_data.gid, tmp_con_data.gid, 16);
  /* save the remote side attributes, we will need it for the post SR */
  res->remote_props = remote_con_data;
  fprintf(stdout, "Remote address = 0x%" PRIx64 "\n", remote_con_data.addr);
  fprintf(stdout, "Remote rkey = 0x%x\n", remote_con_data.rkey);
  fprintf(stdout, "Remote QP number = 0x%x\n", remote_con_data.qp_num);
  fprintf(stdout, "Remote LID = 0x%x\n", remote_con_data.lid);
  if (config.gid_idx >= 0) {
    uint8_t *p = remote_con_data.gid;
    fprintf(stdout,
            "Remote GID = "
            "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%"
            "02x:%02x:%02x\n",
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10],
            p[11], p[12], p[13], p[14], p[15]);
  }
  /* modify the QP to init */
  rc = modify_qp_to_init(res->qp);
  if (rc) {
    fprintf(stderr, "change QP state to INIT failed\n");
    goto connect_qp_exit;
  }
  /* let the client post RR to be prepared for incoming messages */
  if (config.server_name)
    123 {
      rc = post_receive(res);
      if (rc) {
        fprintf(stderr, "failed to post RR\n");
        goto connect_qp_exit;
      }
    }
  /* modify the QP to RTR */
  rc = modify_qp_to_rtr(res->qp, remote_con_data.qp_num, remote_con_data.lid,
                        remote_con_data.gid);
  if (rc) {
    fprintf(stderr, "failed to modify QP state to RTR\n");
    goto connect_qp_exit;
  }
  rc = modify_qp_to_rts(res->qp);
  if (rc) {
    fprintf(stderr, "failed to modify QP state to RTR\n");
    goto connect_qp_exit;
  }
  fprintf(stdout, "QP state was change to RTS\n");
  /* sync to make sure that both sides are in states that they can connect to
   * prevent packet loose */
  if (sock_sync_data(res->sock, 1, "Q",
                     &temp_char)) /* just send a dummy char back and forth */
  {
    fprintf(stderr, "sync error after QPs are were moved to RTS\n");
    rc = 1;
  }
connect_qp_exit:
  return rc;
}

int main() {
    printf("This is a server\n");
    if (open_mlx_device("mlx5_0")) {
        printf("Open device failed");
    }

    struct ibv_context* context = createContext("mlx5_0");

    // 2.2 create protection domain
    struct ibv_pd* protection_domain = ibv_alloc_pd(context);

    // 3. create completion queue
    // minimum requested value
    // max_cqe for my device is 4194303
    int cq_size = 0xff;
    struct ibv_cq* completion_queue = ibv_create_cq(context, cq_size, nullptr, nullptr, 0);
    printf("Completion queue size %d\n", completion_queue->cqe);
    // notice here that CQ size is always rounded up to the next 0xff..ff

    // 4. Create a queue pair
    struct ibv_qp* queue_pair = createQueuePair(protection_domain, completion_queue);
    printf("QP state %d\n", queue_pair->state);

    // Right after created, the queue pair has a state RESET(0).
    // With this state the queue pair does not work.
    // We have to establish queue pair connection with another queue pair to make it work.

    // The steps are as following
    //  Call modify_qp_to_init.
    //  Call post_receive.
    //  Call sock_sync_data to exchange information between server and client.
    //  Call modify_qp_to_rtr. (ready to read)
    //  Call modify_qp_to_rts.
    //  Call sock_sync_data to synchronize client<->server


    // We first change the QP state to INIT(1)
    if (!changeQueuePairStateToInit(queue_pair)) {
      printf("changeQueuePairStateToInit failed \n");
    }
    printf("QP state %d\n", queue_pair->state);

    changeQueuePairStateToRTR(queue_pair, 1, )





};