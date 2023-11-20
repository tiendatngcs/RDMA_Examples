/* 
 * Compile Command:
 * g++ remote.cc -o remote.exe -libverbs -lrdmacm -pthread
 * 
 * 
 * Run remote code first, then local code
 * ./remote_node.exe -a 192.168.0.21 -p 7471 -s 32
 */

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>


static const char *local_addr;
static const char *port ;
static size_t rm_size;

static struct rdma_cm_id *listen_id, *id;
struct rdma_addrinfo hints, *res;
struct ibv_wc wc;
static struct ibv_mr *mr, *send_mr, *rdma_mr, *local_mr, *rdma_mr2;
static int send_flags;

#define BUFFER_SIZE 16
static uint8_t send_msg[BUFFER_SIZE];
static uint8_t recv_msg[BUFFER_SIZE];

static uint8_t* rdma_buff;

static uint8_t* buff_arr[100];
static struct ibv_mr* mr_arr[100];
int next_empty_idx = 0;

// #define RDMA_MR_SIZE 1 << 12 // 4kb

// static uint8_t rdma_buff[RDMA_MR_SIZE];

enum DISCONNECT_CODE {
    // out_destroy_listen_ep,
    // out_free_addrinfo,
    // out_destroy_accept_ep,
    // out_dereg_recv,
    // out_dereg_send,
    // out_disconnect,


	out_dereg_rdma_mr,
	out_disconnect,
	out_dereg_send,
	out_dereg_recv,
	out_destroy_ep,
	out_free_addrinfo,
	out_dereg_sendout,


};

enum COMM_CODE {
    BEGIN,
	REG_MR,
	EXIT,
    TEST = 10
};


//=============================Utils================================

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
}


static void get_wc_status(struct ibv_wc* wc) {
    printf("WC Status: %d\n", wc->status);
    printf("WC Opcode: %d\n", wc->opcode);
    printf("--------------\n");
}

static bool is_exit_signal(char* buffer, char* exit_signal) {
	if (strcmp(buffer, exit_signal) == 0) return true;
	return false;
}

void out_func(DISCONNECT_CODE code) {
    printf("out_func ------------------------------\n");
	switch (code)
	{
	case out_dereg_rdma_mr:
		for (int i = 0 ; i < next_empty_idx; i ++) {
			rdma_dereg_mr(mr_arr[i]);

		}
	case out_disconnect:
		rdma_disconnect(id);
	case out_dereg_send:
		if ((send_flags & IBV_SEND_INLINE) == 0)
			rdma_dereg_mr(send_mr);
	case out_dereg_recv:
		rdma_dereg_mr(mr);
	case out_destroy_ep:
		rdma_destroy_ep(id);
	case out_free_addrinfo:
		rdma_freeaddrinfo(res);
	default:
		break;
	}
	exit(1);
}



void create_new_mr(struct rdma_cm_id* id, size_t length, unsigned int access_flags) {
	// create new mr and send rkey and addr to server
	int ret, curr_buff_idx;
	struct ibv_mr* mr;
	printf("creating new mr\n");
	curr_buff_idx = next_empty_idx;
	buff_arr[curr_buff_idx] = (uint8_t*)malloc(length);
	printf("Alloc addr: %p\n", buff_arr[curr_buff_idx]);
	mr_arr[curr_buff_idx] = ibv_reg_mr(id->pd, buff_arr[curr_buff_idx], length, access_flags);
	printf("MR: %p\n", mr_arr[curr_buff_idx]);
	printf("MR starting addr: %p\n", mr_arr[curr_buff_idx]->addr);
	mr = mr_arr[curr_buff_idx];
	if (mr == NULL) {
		printf("Failed to register a new MR\n");
		out_func(out_disconnect);
	}

	// copy mr remote key and addr to send buffer to send to server
	print_a_buffer((uint8_t*)&mr->rkey, sizeof(mr->rkey), "rkey");
	print_a_buffer((uint8_t*)&mr->addr, sizeof(mr->addr), "addr");
	memcpy(send_msg, &mr->rkey, sizeof(mr->rkey));
	memcpy(send_msg+sizeof(mr->rkey), &mr->addr, sizeof(mr->addr));

	print_buffers();

	// now send buffer to server
	printf("creating new mr\n");
	ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	if (ret) {
		perror("rdma_post_send");
		out_func(out_dereg_rdma_mr);
	}

	while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	if (ret < 0) {
		perror("rdma_get_send_comp");
		out_func(out_dereg_rdma_mr);
	}
	next_empty_idx ++;
}


COMM_CODE get_comm_code() {
	int ret;
	uint8_t code_buff[4]; // 4 byte buffer to get comm code
	uint8_t rm_size_buff[8];
	COMM_CODE code;
    memset(recv_msg, 0, BUFFER_SIZE);
	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
		out_func(out_dereg_send);
	}

	while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	if (ret < 0)
		perror("rdma_get_recv_comp");
	else
		ret = 0;

	memcpy(code_buff, recv_msg, sizeof(COMM_CODE));
	code = (COMM_CODE)*code_buff;

	if (code == REG_MR) {
		printf("REG_MR code\n");
		memcpy(rm_size_buff, recv_msg + sizeof(COMM_CODE), sizeof(size_t));
		print_a_buffer(rm_size_buff, 8, "rm_size_buff");
		printf("rm_size: %lu\n", *(size_t*)rm_size_buff);
		rm_size = *(size_t*)rm_size_buff;
		printf("rm_size: %lu\n", rm_size);
	}

	return code;
}

static int remote_node_run()
{
	// struct rdma_addrinfo hints, *res;
	struct ibv_qp_init_attr attr;
	// struct ibv_wc wc;
	int ret;
	void* temp;
	COMM_CODE code;



	// rdma_buff = (uint8_t*)malloc(rm_size);


    // test filling buffer
    // strcpy(send_msg, "Dat");


	printf("rdma_getaddrinfo\n");
	memset(&hints, 0, sizeof hints);
	hints.ai_port_space = RDMA_PS_TCP;
	ret = rdma_getaddrinfo(local_addr, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
		goto out;
	}

	printf("rdma_create_ep\n");
	memset(&attr, 0, sizeof attr);
	attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
	attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
	attr.cap.max_inline_data = 16;
	attr.qp_context = id;
	attr.sq_sig_all = 1;
	ret = rdma_create_ep(&id, res, NULL, &attr);
	// Check to see if we got inline data allowed or not
	if (attr.cap.max_inline_data >= 16)
		send_flags = IBV_SEND_INLINE;
	else
		printf("rdma_client: device doesn't support IBV_SEND_INLINE, "
		       "using sge sends\n");

	if (ret) {
		perror("rdma_create_ep");
		goto out_free_addrinfo;
	}

	mr = rdma_reg_msgs(id, recv_msg, 16);
	if (!mr) {
		perror("rdma_reg_msgs for recv_msg");
		ret = -1;
		goto out_destroy_ep;
	}
	if ((send_flags & IBV_SEND_INLINE) == 0) {
		send_mr = rdma_reg_msgs(id, send_msg, 16);
		if (!send_mr) {
			perror("rdma_reg_msgs for send_msg");
			ret = -1;
			goto out_dereg_recv;
		}
	}

	// ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	// if (ret) {
	// 	perror("rdma_post_recv");
	// 	goto out_dereg_send;
	// }

	ret = rdma_connect(id, NULL);
	if (ret) {
		perror("rdma_connect");
		goto out_dereg_send;
	}

	// ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	// if (ret) {
	// 	perror("rdma_post_send");
	// 	goto out_disconnect;
	// }

	// while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	// if (ret < 0) {
	// 	perror("rdma_get_send_comp");
	// 	goto out_disconnect;
	// }

	// while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	// if (ret < 0)
	// 	perror("rdma_get_recv_comp");
	// else
	// 	ret = 0;


    // ---------------------------------------------

	// new code

	// Create new mr for RDMA read and write to this server
	// rdma_mr = ibv_reg_mr(id->pd, rdma_buff, rm_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
	// // rdma_mr2 = ibv_reg_mr(id->pd, rdma_buff2, rm_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
	
	// // copy mr remote key and addr to send buffer to send to server
	// print_a_buffer((uint8_t*)&rdma_mr->rkey, sizeof(rdma_mr->rkey), "rkey");
	// print_a_buffer((uint8_t*)&rdma_mr->addr, sizeof(rdma_mr->addr), "addr");
	// // print_a_buffer((uint8_t*)&rdma_mr2->rkey, sizeof(rdma_mr->rkey), "rkey2");
	// // print_a_buffer((uint8_t*)&rdma_mr2->addr, sizeof(rdma_mr->addr), "addr2");
	// temp = memcpy(send_msg, &rdma_mr->rkey, sizeof(rdma_mr->rkey));
	// temp = memcpy(send_msg+sizeof(rdma_mr->rkey), &rdma_mr->addr, sizeof(rdma_mr->addr));

	// print_buffers();

	// // now send buffer to server
	// ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	// if (ret) {
	// 	perror("rdma_post_send");
	// 	goto out_dereg_rdma_mr;
	// }

	// while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	// if (ret < 0) {
	// 	perror("rdma_get_send_comp");
	// 	goto out_dereg_rdma_mr;
	// }


	// rdma_mr = create_new_mr(id, rdma_buff, rm_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

	// Post a recv and wait for signal from server to exit
	// ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	// if (ret) {
	// 	perror("rdma_post_recv");
	// 	goto out_dereg_send;
	// }

	// while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	// if (ret < 0)
	// 	perror("rdma_get_recv_comp");
	// else
	// 	ret = 0;

	while ((code = get_comm_code()) != COMM_CODE::EXIT) {
		// code operations here
		switch (code) {
			case COMM_CODE::REG_MR:
				create_new_mr(id, rm_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
                break;
			case COMM_CODE::TEST:
				printf("Test code \n");
                break;
            case COMM_CODE::BEGIN:
                printf("Begin code \n");
                break;
		}
		printf("Code: %d\n", code);
        printf("------------------------- End of comm code ------------------------- \n");
        
	}

	

out_dereg_rdma_mr:
    for (int i = next_empty_idx - 1; i >= 0; i--){
		printf("%d\n", i);
        printf("%p, %p\n", buff_arr[i], mr_arr[i]);
        // free(buff_arr[i]);
        ret = rdma_dereg_mr(mr_arr[i]);
        if (ret) {
            perror("rdma_post_send");
        }
    }
out_disconnect:
	rdma_disconnect(id);
out_dereg_send:
	if ((send_flags & IBV_SEND_INLINE) == 0)
		rdma_dereg_mr(send_mr);
out_dereg_recv:
	rdma_dereg_mr(mr);
out_destroy_ep:
	rdma_destroy_ep(id);
out_free_addrinfo:
	rdma_freeaddrinfo(res);
out:
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
			rm_size = (size_t)1<<atoi(optarg);
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-a server_addr]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-s size_of_remote_mem_shift]\n");
			exit(1);
		}
	}

    printf("Address of local node %s\n", local_addr);
    printf("Port of local node %s\n", port);
	printf("Size of size_t %ld\n", sizeof(size_t));
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

    remote_node_run();
}