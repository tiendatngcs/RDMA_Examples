/* 
 * Compile Command:
 * g++ local.cc -o local.exe -libverbs -lrdmacm -pthread
 * 
 * 
 * Run remote code first, then local code
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <thread>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>


static const char *local_addr;
static const char *port ;
static size_t rm_size;

static struct rdma_cm_id *listen_id, *id;
static struct ibv_mr *mr, *send_mr, *local_mr;
struct rdma_addrinfo *res;
struct ibv_wc wc;
static int send_flags;


// static struct rdma_cm_id *listen_id2, *id2;
// static struct ibv_mr *mr2, *send_mr2, *local_mr2;
// static int send_flags2;

#define BUFFER_SIZE 16
static uint8_t send_msg[BUFFER_SIZE];
static uint8_t recv_msg[BUFFER_SIZE];

#define NUM_THREADS 2

// static uint8_t send_msg2[BUFFER_SIZE];
// static uint8_t recv_msg2[BUFFER_SIZE];
// static uint8_t buff_local_send2[BUFFER_SIZE];

// static uint32_t rkey = 0;  // rkey of remote memory to be used at local node
// static void* remote_addr = 0;	// addr of remote memory to be used at local node

// static uint32_t rkey2 = 0;  // rkey of remote memory to be used at local node
// static void* remote_addr2 = 0;	// addr of remote memory to be used at local node


enum COMM_CODE {
    BEGIN,
	REG_MR,
	EXIT,
    TEST = 10
};

enum DISCONNECT_CODE {
    out_disconnect,
    out_dereg_send,
    out_dereg_recv,
    out_destroy_accept_ep,
    out_destroy_listen_ep,
    out_free_addrinfo,


	// out_dereg_rdma_mr,
	// out_disconnect,
	// out_dereg_send,
	// out_dereg_recv,
	// out_destroy_ep,
	// out_free_addrinfo,
	// out_dereg_sendout,


};

static void print_a_buffer(uint8_t* buffer, int buffer_size, std::string buffer_name) {
    char* c = const_cast<char*>(buffer_name.c_str());
	printf("%s: ", c);
    for (int i = 0; i < buffer_size; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
}

static void print_buffers() {
	print_a_buffer(send_msg, BUFFER_SIZE, (char*)"send_msg");
	print_a_buffer(recv_msg, BUFFER_SIZE, (char*)"recv_msg");
    printf("-----------\n");
	// print_a_buffer(send_msg2, BUFFER_SIZE, (char*)"send_msg2");
	// print_a_buffer(recv_msg2, BUFFER_SIZE, (char*)"recv_msg2");
    // printf("-----------\n");
}


static void get_wc_status(struct ibv_wc* wc) {
    printf("WC Status: %d\n", wc->status);
    printf("WC Opcode: %d\n", wc->opcode);
    printf("--------------\n");
}

void out_func(DISCONNECT_CODE code) {
	switch (code)
	{
        case out_disconnect:
            rdma_disconnect(id);
        case out_dereg_send:
            if ((send_flags & IBV_SEND_INLINE) == 0)
                rdma_dereg_mr(send_mr);
        case out_dereg_recv:
            rdma_dereg_mr(mr);
        case out_destroy_accept_ep:
            rdma_destroy_ep(id);
        case out_destroy_listen_ep:
            rdma_destroy_ep(listen_id);
        case out_free_addrinfo:
            rdma_freeaddrinfo(res);
        default:
            break;
	}
	exit(1);
}

int send_comm_code(COMM_CODE code) {

    int ret;
    // now we can close both process by posting a send

	printf("rdma_post_send ...\n");
    memset(send_msg, 0, BUFFER_SIZE);
    memcpy(send_msg, &code, sizeof(COMM_CODE));
    print_a_buffer(send_msg, BUFFER_SIZE, "send_msg");
	ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	if (ret) {
		perror("rdma_post_send");
		out_func(out_disconnect);
	}

    printf("rdma_get_send_comp ...\n");
	while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	if (ret < 0)
		perror("rdma_get_send_comp");
	else
		ret = 0;

    return ret;
}

int reg_new_mr(uint32_t* rkey, void** remote_addr) {
    printf("Registering new mr at remote .. \n");
    int ret;
    send_comm_code(COMM_CODE::REG_MR);
    	// receive remote mr rkey and addr of client
	printf("rdma_post_recv ...\n");
	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
		out_func(out_dereg_send);
	}

    print_buffers();

	printf("rdma_get_recv_comp ...\n");
	while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	if (ret < 0) {
		perror("rdma_get_recv_comp");
		out_func(out_disconnect);
	}
    print_buffers();

	// extract information

	memcpy(rkey, recv_msg, sizeof(uint32_t));
	print_a_buffer((uint8_t*)rkey, sizeof(uint32_t), "rkey");

	memcpy(remote_addr, recv_msg + sizeof(uint32_t), sizeof(void*));

	print_a_buffer((uint8_t*)remote_addr, sizeof(void*), "remote addr");
    return ret;
}

void thread_do (int thread_idx, uint32_t rkey, void* remote_addr) {
    int ret;
    uint8_t* buff_local_send =  (uint8_t*) malloc(rm_size);
    memset(buff_local_send, 0, rm_size);

	local_mr = ibv_reg_mr(id->pd, buff_local_send, rm_size, IBV_ACCESS_LOCAL_WRITE);
    int val = 99;

    printf("RDMA write ... thread %d\n", thread_idx);
	strcpy((char*)buff_local_send, std::to_string(thread_idx).c_str());
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");
	ret = rdma_post_write(id, NULL, buff_local_send, rm_size, local_mr, IBV_SEND_SIGNALED, (uint64_t)remote_addr, rkey);
	if (ret) {
        perror("Error rdma write\n");
		out_func(out_disconnect);
    }
	do {
        ret = ibv_poll_cq(id->send_cq, 1, &wc);
    } while (ret == 0);

	
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	get_wc_status(&wc);

	// Now try reading from remote mem
	printf("RDMA read ... thread %d\n", thread_idx);

	memset(buff_local_send, 0, sizeof(buff_local_send));
	print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	ret = rdma_post_read(id, NULL, buff_local_send, rm_size, local_mr, IBV_SEND_SIGNALED, (uint64_t)remote_addr, rkey);
	if (ret) {
        perror("Error rdma read\n");
		out_func(out_disconnect);
    }
	do {
        ret = ibv_poll_cq(id->send_cq, 1, &wc);
    } while (ret == 0);
    memcpy(&val, buff_local_send, sizeof(int));
    printf("Thread %d, val %d\n", thread_idx, val);
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	get_wc_status(&wc);

}

int local_node_run () {
    struct rdma_addrinfo hints;
	struct ibv_qp_init_attr init_attr;
	struct ibv_qp_attr qp_attr;
	int ret;


    uint32_t rkey = 0;  // rkey of remote memory to be used at local node
    void* remote_addr = 0;	// addr of remote memory to be used at local node

    uint32_t rkey2 = 0;  // rkey of remote memory to be used at local node
    void* remote_addr2 = 0;	// addr of remote memory to be used at local node

    printf("creating rdma buffer\n");
    uint8_t* buff_local_send =  (uint8_t*) malloc(rm_size);
    printf("rdma buffer created\n");
    print_buffers();

    printf("finish printing\n");
	memset(&hints, 0, sizeof hints);
	hints.ai_flags = RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_TCP;
	ret = rdma_getaddrinfo(local_addr, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	memset(&init_attr, 0, sizeof init_attr);
	init_attr.cap.max_send_wr = init_attr.cap.max_recv_wr = 16;
	init_attr.cap.max_send_sge = init_attr.cap.max_recv_sge = 16;
	init_attr.cap.max_inline_data = 16;
	init_attr.sq_sig_all = 1;
	ret = rdma_create_ep(&listen_id, res, NULL, &init_attr);
	if (ret) {
		perror("rdma_create_ep");
        out_func(out_free_addrinfo);
		// goto out_free_addrinfo;
	}
    printf("Server listening ...\n");
	ret = rdma_listen(listen_id, 0);
	if (ret) {
		perror("rdma_listen");
		// goto out_destroy_listen_ep;
        out_func(out_destroy_listen_ep);
	}


    printf("Getting connection request ...\n");
	ret = rdma_get_request(listen_id, &id);
	if (ret) {
		perror("rdma_get_request");
		// goto out_destroy_listen_ep;
        out_func(out_destroy_listen_ep);
	}

    print_buffers();

	memset(&qp_attr, 0, sizeof qp_attr);
	memset(&init_attr, 0, sizeof init_attr);
	ret = ibv_query_qp(id->qp, &qp_attr, IBV_QP_CAP,
			   &init_attr);
	if (ret) {
		perror("ibv_query_qp");
		// goto out_destroy_accept_ep;
        out_func(out_destroy_accept_ep);
	}
	if (init_attr.cap.max_inline_data >= 16)
		send_flags = IBV_SEND_INLINE;
	else
		printf("rdma_server: device doesn't support IBV_SEND_INLINE, "
		       "using sge sends\n");

    printf("Registering mr ...\n");
	mr = rdma_reg_msgs(id, recv_msg, 16);
	if (!mr) {
		ret = -1;
		perror("rdma_reg_msgs for recv_msg");
		// goto out_destroy_accept_ep;
        out_func(out_destroy_accept_ep);
	}
	if ((send_flags & IBV_SEND_INLINE) == 0) {
        printf("Registering mr ...");
		send_mr = rdma_reg_msgs(id, send_msg, 16);
		if (!send_mr) {
			ret = -1;
			perror("rdma_reg_msgs for send_msg");
			// goto out_dereg_recv;
            out_func(out_dereg_recv);
		}
	}
    print_buffers();

    // printf("rdma_post_recv ...\n");
	// ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	// if (ret) {
	// 	perror("rdma_post_recv");
	// 	goto out_dereg_send;
	// }

    // print_buffers();

    
    printf("Server accepting connection ...\n");
	ret = rdma_accept(id, NULL);
	if (ret) {
		perror("rdma_accept");
		// goto out_dereg_send;
        out_func(out_dereg_send);
	}
    print_buffers();



    // printf("rdma_get_recv_comp ...\n");
	// while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	// if (ret < 0) {
	// 	perror("rdma_get_recv_comp");
	// 	goto out_disconnect;
	// }
    // print_buffers();

    // printf("rdma_post_send ...\n");
	// ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	// if (ret) {
	// 	perror("rdma_post_send");
	// 	goto out_disconnect;
	// }
    // print_buffers();

    // printf("rdma_get_send_comp ...\n");
	// while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	// if (ret < 0)
	// 	perror("rdma_get_send_comp");
	// else
	// 	ret = 0;

    // print_buffers();

    // ---------------------------------------------

	// new code here
    printf("Sending codes to Remote:\n");


    ret = reg_new_mr(&rkey, &remote_addr);
    ret = reg_new_mr(&rkey2, &remote_addr2);

	
	// // Write to remote mem first
	// printf("RDMA write ...\n");
	// strcpy((char*)buff_local_send, "Dat");
	
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");
	// local_mr = ibv_reg_mr(id->pd, buff_local_send, rm_size, IBV_ACCESS_LOCAL_WRITE);
	// ret = rdma_post_write(id, NULL, buff_local_send, rm_size, local_mr, IBV_SEND_SIGNALED, (uint64_t)remote_addr, rkey);
	// if (ret) {
    //     perror("Error rdma write\n");
    //     goto out_disconnect;
    // }
	// do {
    //     ret = ibv_poll_cq(id->send_cq, 1, &wc);
    // } while (ret == 0);

	
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	// get_wc_status(&wc);

	// // Now try reading from remote mem
	// printf("RDMA read ...\n");

	// memset(buff_local_send, 0, sizeof(buff_local_send));
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	// ret = rdma_post_read(id, NULL, buff_local_send, rm_size, local_mr, IBV_SEND_SIGNALED, (uint64_t)remote_addr, rkey);
	// if (ret) {
    //     perror("Error rdma read\n");
    //     goto out_disconnect;
    // }
	// do {
    //     ret = ibv_poll_cq(id->send_cq, 1, &wc);
    // } while (ret == 0);

	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	// get_wc_status(&wc);



    // // ------ testing wr to a differnt buffer


    // printf("RDMA write ...\n");
	// strcpy((char*)buff_local_send, "Nguyen");
	
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");
	// // local_mr = ibv_reg_mr(id->pd, buff_local_send, rm_size, IBV_ACCESS_LOCAL_WRITE);
	// ret = rdma_post_write(id, NULL, buff_local_send, rm_size, local_mr, IBV_SEND_SIGNALED, (uint64_t)remote_addr2, rkey2);
	// if (ret) {
    //     perror("Error rdma write\n");
    //     goto out_disconnect;
    // }
	// do {
    //     ret = ibv_poll_cq(id->send_cq, 1, &wc);
    // } while (ret == 0);

	
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	// get_wc_status(&wc);

	// // Now try reading from remote mem
	// printf("RDMA read ...\n");

	// memset(buff_local_send, 0, sizeof(buff_local_send));
	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	// ret = rdma_post_read(id, NULL, buff_local_send, rm_size, local_mr, IBV_SEND_SIGNALED, (uint64_t)remote_addr2, rkey2);
	// if (ret) {
    //     perror("Error rdma read\n");
    //     goto out_disconnect;
    // }
	// do {
    //     ret = ibv_poll_cq(id->send_cq, 1, &wc);
    // } while (ret == 0);

	// print_a_buffer(buff_local_send, BUFFER_SIZE, "buff_local_send");

	// get_wc_status(&wc);

    // threading tests

    std::thread threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = std::thread (thread_do, i, rkey, remote_addr);
        // threads[i].join();
    }

    // printf("Joining threads\n");
    // // join
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i].join();
    }

    // thread_do(0, rkey, remote_addr);



    // send_comm_code(COMM_CODE::TEST);
    send_comm_code(COMM_CODE::EXIT);

// out_disconnect:
// 	rdma_disconnect(id);
// out_dereg_send:
// 	if ((send_flags & IBV_SEND_INLINE) == 0)
// 		rdma_dereg_mr(send_mr);
// out_dereg_recv:
// 	rdma_dereg_mr(mr);
// out_destroy_accept_ep:
// 	rdma_destroy_ep(id);
// out_destroy_listen_ep:
// 	rdma_destroy_ep(listen_id);
// out_free_addrinfo:
// 	rdma_freeaddrinfo(res);

    out_func(out_disconnect);
	return ret;

}



int main (int argc, char** argv) {
    int ret, op;
    int is_server = 0;

    while ((op = getopt(argc, argv, "a:p:s:")) != -1) {
		switch (op) {
		// case 's':
		// 	is_server = 1;
		// 	break;
        case 'a':
            local_addr = optarg;
            break;
		case 'p':
			port = optarg;
			break;
		case 's':
			// rm_size = (size_t)1<<atoi(optarg);
            rm_size = atoi(optarg);
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-a server_addr]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-s size_of_remote_mem_shift]\n");
			exit(1);
		}
	}

    printf("Address of remote node %s\n", local_addr);
    printf("Port of local node %s\n", port);
    printf("Size of Remote Memory %lu\n", rm_size);

    // if (is_server) {
    //     printf("rdma_server: start\n");
    //     ret = server_run();
    //     printf("rdma_server: end %d\n", ret);
    // } else {
    //     printf("rdma_client: start\n");
    //     ret = client_run();
    //     printf("rdma_client: end %d\n", ret);
        
    // }

    local_node_run();
}