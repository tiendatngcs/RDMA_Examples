/* 
 * Compile Command:
 * gcc rdma_example.c -o rdma_example.exe -libverbs -lrdmacm -pthread
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define DEFAULT_SERVER_ADDR "192.168.0.21"
#define DEFAULT_PORT "7471"

static const char *server_addr;
static const char *port ;

static struct rdma_cm_id *listen_id, *id;
static struct ibv_mr *mr, *send_mr;
static int send_flags;

#define BUFFER_SIZE 16
static uint8_t send_msg[BUFFER_SIZE];
static uint8_t recv_msg[BUFFER_SIZE];

static uint8_t buff1[BUFFER_SIZE];
static uint8_t buff2[BUFFER_SIZE];

static void print_buffers() {
    printf("send_msg: ");
    for (int i = 0; i < BUFFER_SIZE; i++) {
        printf("%c", send_msg[i]);
    }
    printf("\n");
    
    printf("recv_msg: ");
    for (int i = 0; i < BUFFER_SIZE; i++) {
        printf("%c", recv_msg[i]);
    }
    printf("\n");
    printf("-----------\n");

    printf("buff1: ");
    for (int i = 0; i < BUFFER_SIZE; i++) {
        printf("%c", buff1[i]);
    }
    printf("\n");
    printf("-----------\n");
}

static void get_wc_status(struct ibv_wc* wc) {
    printf("WC Status: %d\n", wc->status);
    printf("WC Opcode: %d\n", wc->opcode);
    printf("--------------\n");
}

static int server_run(void)
{
	struct rdma_addrinfo hints, *res;
	struct ibv_qp_init_attr init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_wc wc;
	int ret;

    print_buffers();

	memset(&hints, 0, sizeof hints);
	hints.ai_flags = RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_TCP;
	ret = rdma_getaddrinfo(server_addr, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	memset(&init_attr, 0, sizeof init_attr);
	init_attr.cap.max_send_wr = init_attr.cap.max_recv_wr = 1;
	init_attr.cap.max_send_sge = init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_inline_data = 16;
	init_attr.sq_sig_all = 1;
	ret = rdma_create_ep(&listen_id, res, NULL, &init_attr);
	if (ret) {
		perror("rdma_create_ep");
		goto out_free_addrinfo;
	}
    printf("Server listening ...\n");
	ret = rdma_listen(listen_id, 0);
	if (ret) {
		perror("rdma_listen");
		goto out_destroy_listen_ep;
	}


    printf("Getting connection request ...\n");
	ret = rdma_get_request(listen_id, &id);
	if (ret) {
		perror("rdma_get_request");
		goto out_destroy_listen_ep;
	}

    print_buffers();

	memset(&qp_attr, 0, sizeof qp_attr);
	memset(&init_attr, 0, sizeof init_attr);
	ret = ibv_query_qp(id->qp, &qp_attr, IBV_QP_CAP,
			   &init_attr);
	if (ret) {
		perror("ibv_query_qp");
		goto out_destroy_accept_ep;
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
		goto out_destroy_accept_ep;
	}
	if ((send_flags & IBV_SEND_INLINE) == 0) {
        printf("Registering mr ...");
		send_mr = rdma_reg_msgs(id, send_msg, 16);
		if (!send_mr) {
			ret = -1;
			perror("rdma_reg_msgs for send_msg");
			goto out_dereg_recv;
		}
	}
    print_buffers();

    printf("rdma_post_recv ...\n");
	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
		goto out_dereg_send;
	}

    print_buffers();

    // ---------------------------------------------
    
    printf("Server accepting connection ...\n");
	ret = rdma_accept(id, NULL);
	if (ret) {
		perror("rdma_accept");
		goto out_dereg_send;
	}
    print_buffers();



    printf("rdma_get_recv_comp ...\n");
	while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	if (ret < 0) {
		perror("rdma_get_recv_comp");
		goto out_disconnect;
	}
    print_buffers();

    printf("rdma_post_send ...\n");
	ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	if (ret) {
		perror("rdma_post_send");
		goto out_disconnect;
	}
    print_buffers();

    printf("rdma_get_send_comp ...\n");
	while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	if (ret < 0)
		perror("rdma_get_send_comp");
	else
		ret = 0;

    print_buffers();

out_disconnect:
	rdma_disconnect(id);
out_dereg_send:
	if ((send_flags & IBV_SEND_INLINE) == 0)
		rdma_dereg_mr(send_mr);
out_dereg_recv:
	rdma_dereg_mr(mr);
out_destroy_accept_ep:
	rdma_destroy_ep(id);
out_destroy_listen_ep:
	rdma_destroy_ep(listen_id);
out_free_addrinfo:
	rdma_freeaddrinfo(res);
	return ret;
}

static int client_run(void)
{
	struct rdma_addrinfo hints, *res;
	struct ibv_qp_init_attr attr;
	struct ibv_wc wc;
	int ret;

    // test filling buffer
    strcpy(send_msg, "Dat");

	memset(&hints, 0, sizeof hints);
	hints.ai_port_space = RDMA_PS_TCP;
	ret = rdma_getaddrinfo(server_addr, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
		goto out;
	}

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

	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
		goto out_dereg_send;
	}

	ret = rdma_connect(id, NULL);
	if (ret) {
		perror("rdma_connect");
		goto out_dereg_send;
	}

	ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	if (ret) {
		perror("rdma_post_send");
		goto out_disconnect;
	}

	while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	if (ret < 0) {
		perror("rdma_get_send_comp");
		goto out_disconnect;
	}

	while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	if (ret < 0)
		perror("rdma_get_recv_comp");
	else
		ret = 0;

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


// static int server_process() {
//     struct rdma_addrinfo hints, *res;
// 	struct ibv_qp_init_attr init_attr;
// 	struct ibv_qp_attr qp_attr;
// 	struct ibv_wc wc;
// 	int ret;

//     // strcpy(send_msg, "DAT");
//     // strcpy(recv_msg, "DAT");
//     strcpy(buff1, "DAT");
    
//     // memset(recv_msg, 0, BUFFER_SIZE);

//     print_buffers();

// 	memset(&hints, 0, sizeof hints);
// 	hints.ai_flags = RAI_PASSIVE;
// 	hints.ai_port_space = RDMA_PS_TCP;
//     // hints.ai_qp_type = IBV_QPT_RC;
// 	ret = rdma_getaddrinfo(server_addr, port, &hints, &res);
// 	if (ret) {
// 		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
// 		return ret;
// 	}

//     // memset(&init_attr, 0, sizeof init_attr);
// 	// init_attr.cap.max_send_wr = init_attr.cap.max_recv_wr = 1;
// 	// init_attr.cap.max_send_sge = init_attr.cap.max_recv_sge = 1;
// 	// init_attr.cap.max_inline_data = 16;
// 	// init_attr.qp_context = id;
// 	// init_attr.sq_sig_all = 1;
// 	// ret = rdma_create_ep(&id, res, NULL, &init_attr);

// 	memset(&init_attr, 0, sizeof init_attr);
// 	init_attr.cap.max_send_wr = init_attr.cap.max_recv_wr = 1;
// 	init_attr.cap.max_send_sge = init_attr.cap.max_recv_sge = 1;
// 	init_attr.cap.max_inline_data = 16;
// 	init_attr.sq_sig_all = 1;
// 	ret = rdma_create_ep(&listen_id, res, NULL, &init_attr);
// 	if (ret) {
// 		perror("rdma_create_ep");
// 		goto out_free_addrinfo;
// 	}
//     printf("Server listening ...\n");
// 	ret = rdma_listen(listen_id, 0);
// 	if (ret) {
// 		perror("rdma_listen");
// 		goto out_destroy_listen_ep;
// 	}

//     print_buffers();

//     printf("Getting connection request ...\n");
// 	ret = rdma_get_request(listen_id, &id);
// 	if (ret) {
// 		perror("rdma_get_request");
// 		goto out_destroy_listen_ep;
// 	}

//     print_buffers();

// 	memset(&qp_attr, 0, sizeof qp_attr);
// 	memset(&init_attr, 0, sizeof init_attr);
// 	ret = ibv_query_qp(id->qp, &qp_attr, IBV_QP_CAP,
// 			   &init_attr);

// 	if (ret) {
// 		perror("ibv_query_qp");
// 		goto out_destroy_accept_ep;
// 	}
// 	if (init_attr.cap.max_inline_data >= 16)
// 		send_flags = IBV_SEND_INLINE;
// 	else
// 		printf("rdma_server: device doesn't support IBV_SEND_INLINE, "
// 		       "using sge sends\n");

//     printf("Registering mr ...\n");

//     // mr is remote, send_mr is local
// 	// mr = rdma_reg_msgs(id, recv_msg, 16);
// 	// if (!mr) {
// 	// 	ret = -1;
// 	// 	perror("rdma_reg_msgs for recv_msg");
// 	// 	goto out_destroy_accept_ep;
// 	// }

//     mr = ibv_reg_mr(id->pd, buff1, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
//     if (!mr) {
//         fprintf(stderr, "Error, ibv_reg_mr() failed\n");
//         return -1;
//     }
// 	// if ((send_flags & IBV_SEND_INLINE) == 0) {
//     // printf("Registering mr ...");
//     // send_mr = rdma_reg_msgs(id, send_msg, 16);
//     // if (!send_mr) {
//     //     ret = -1;
//     //     perror("rdma_reg_msgs for send_msg");
//     //     goto out_dereg_recv;
//     // }
// 	// }
//     print_buffers();

//     printf("Server accepting connection ...\n");
// 	ret = rdma_accept(id, NULL);
// 	if (ret) {
// 		perror("rdma_accept");
// 		goto out_dereg_send;
// 	}
//     print_buffers();



//     printf("qp state: %d\n", id->qp->state);
//     printf("mr rkey: %d\n", mr->rkey);
//     printf("mr lkey: %d\n", mr->lkey);



//     // memcpy(&server_pdata, event->param.conn.private_data, sizeof server_pdata);
//     // test rdma write
//     printf("RDMA Write ...\n");
//     ret = rdma_post_write(id, id->context, buff1, 16, mr, IBV_SEND_SIGNALED, 0, mr->rkey);

// 	// ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
//     if (ret) {
//         perror("Error rdma write\n");
//         goto out_dereg_send;
//     }

//     do {
//         ret = ibv_poll_cq(id->send_cq, 1, &wc);
//     } while (ret == 0);

//     get_wc_status(&wc);

//     print_buffers();



//     // strcpy(send_msg, "Not this str");
//     // print_buffers();

//     // printf("RDMA Read ...\n");
//     // ret = rdma_post_read(id, id->context, send_mr->addr, BUFFER_SIZE, send_mr, IBV_SEND_SIGNALED, mr->addr, mr->rkey);
//     // if (ret) {
//     //     perror("Error rdma read\n");
//     //     goto out_dereg_send;
//     // }
//     // do {
//     //     ret = ibv_poll_cq(id->send_cq, 1, &wc);
//     //     printf("still waiting\n");
//     // } while (ret == 0);
//     // print_buffers();

// 	// ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);

// // out_disconnect:
// // 	rdma_disconnect(id);
// out_dereg_send:
// 	if ((send_flags & IBV_SEND_INLINE) == 0)
// 		rdma_dereg_mr(send_mr);
// out_dereg_recv:
// 	// rdma_dereg_mr(mr);
// out_destroy_accept_ep:
// 	rdma_destroy_ep(id);
// out_destroy_listen_ep:
// 	rdma_destroy_ep(listen_id);
// out_free_addrinfo:
// 	rdma_freeaddrinfo(res);
// 	return ret;
// }


// static int client_process() {
//     struct rdma_addrinfo hints, *res;
// 	struct ibv_qp_init_attr attr;
// 	struct ibv_wc wc;
// 	int ret;

//     // test filling buffer
//     strcpy(send_msg, "Dat");

// 	memset(&hints, 0, sizeof hints);
// 	hints.ai_port_space = RDMA_PS_TCP;
// 	ret = rdma_getaddrinfo(server_addr, port, &hints, &res);
// 	if (ret) {
// 		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
// 		goto out;
// 	}



// 	memset(&attr, 0, sizeof attr);
// 	attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
// 	attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
// 	attr.cap.max_inline_data = 16;
// 	attr.qp_context = id;
// 	attr.sq_sig_all = 1;
// 	ret = rdma_create_ep(&id, res, NULL, &attr);
// 	// Check to see if we got inline data allowed or not
// 	if (attr.cap.max_inline_data >= 16)
// 		send_flags = IBV_SEND_INLINE;
// 	else
// 		printf("rdma_client: device doesn't support IBV_SEND_INLINE, "
// 		       "using sge sends\n");

// 	if (ret) {
// 		perror("rdma_create_ep");
// 		goto out_free_addrinfo;
// 	}

//     printf("%d\n", id->qp->state);

// 	// mr = rdma_reg_msgs(id, recv_msg, 16);
// 	// if (!mr) {
// 	// 	perror("rdma_reg_msgs for recv_msg");
// 	// 	ret = -1;
// 	// 	goto out_destroy_ep;
// 	// }
// 	// if ((send_flags & IBV_SEND_INLINE) == 0) {
// 	// 	send_mr = rdma_reg_msgs(id, send_msg, 16);
// 	// 	if (!send_mr) {
// 	// 		perror("rdma_reg_msgs for send_msg");
// 	// 		ret = -1;
// 	// 		goto out_dereg_recv;
// 	// 	}
// 	// }

// 	// ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
// 	// if (ret) {
// 	// 	perror("rdma_post_recv");
// 	// 	goto out_dereg_send;
// 	// }

// 	ret = rdma_connect(id, NULL);
// 	if (ret) {
// 		perror("rdma_connect");
// 		goto out_dereg_send;
// 	}

//     printf("%d\n", id->qp->state);

//     while(1);

// // out_disconnect:
// // 	rdma_disconnect(id);
// out_dereg_send:
// 	// if ((send_flags & IBV_SEND_INLINE) == 0)
// 	// 	rdma_dereg_mr(send_mr);
// // out_dereg_recv:
// // 	rdma_dereg_mr(mr);
// // out_destroy_ep:
// 	rdma_destroy_ep(id);
// out_free_addrinfo:
// 	rdma_freeaddrinfo(res);
// out:
// 	return ret;
// }

// static int remote_process () {
//     struct rdma_addrinfo hints, *res;
// 	struct ibv_qp_init_attr init_attr;
// 	struct ibv_qp_attr qp_attr;
// 	struct ibv_wc wc;
// 	int ret;
//     uint32_t my_key;
//     uint64_t my_addr;

//     // test filling buffer
//     strcpy(send_msg, "Dat");

//     print_buffers();

// 	memset(&hints, 0, sizeof hints);
// 	hints.ai_flags = RAI_PASSIVE;
// 	hints.ai_port_space = RDMA_PS_TCP;
//     // hints.ai_qp_type = IBV_QPT_RC;
// 	ret = rdma_getaddrinfo(server_addr, port, &hints, &res);
// 	if (ret) {
// 		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
// 		return ret;
// 	}

// 	memset(&init_attr, 0, sizeof init_attr);
// 	init_attr.cap.max_send_wr = init_attr.cap.max_recv_wr = 1;
// 	init_attr.cap.max_send_sge = init_attr.cap.max_recv_sge = 1;
// 	init_attr.cap.max_inline_data = 16;
// 	init_attr.sq_sig_all = 1;
// 	ret = rdma_create_ep(&listen_id, res, NULL, &init_attr);
// 	if (ret) {
// 		perror("rdma_create_ep");
// 		goto out_free_addrinfo;
// 	}
//     printf("Server listening ...\n");
// 	ret = rdma_listen(listen_id, 0);
// 	if (ret) {
// 		perror("rdma_listen");
// 		goto out_destroy_listen_ep;
// 	}

//     print_buffers();

//     printf("Getting connection request ...\n");
// 	ret = rdma_get_request(listen_id, &id);
// 	if (ret) {
// 		perror("rdma_get_request");
// 		goto out_destroy_listen_ep;
// 	}

//     print_buffers();

// 	memset(&qp_attr, 0, sizeof qp_attr);
// 	memset(&init_attr, 0, sizeof init_attr);
// 	ret = ibv_query_qp(id->qp, &qp_attr, IBV_QP_CAP,
// 			   &init_attr);

// 	if (ret) {
// 		perror("ibv_query_qp");
// 		goto out_destroy_accept_ep;
// 	}
// 	if (init_attr.cap.max_inline_data >= 16)
// 		send_flags = IBV_SEND_INLINE;
// 	else
// 		printf("rdma_server: device doesn't support IBV_SEND_INLINE, "
// 		       "using sge sends\n");

//     printf("Registering mr ...\n");

//     // mr is remote, send_mr is local
// 	// mr = rdma_reg_msgs(id, recv_msg, 16);
// 	// if (!mr) {
// 	// 	ret = -1;
// 	// 	perror("rdma_reg_msgs for recv_msg");
// 	// 	goto out_destroy_accept_ep;
// 	// }

//     mr = ibv_reg_mr(id->pd, buff1, BUFFER_SIZE, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
//     if (!mr) {
//         fprintf(stderr, "Error, ibv_reg_mr() failed\n");
//         return -1;
//     }

//     // create a mr to exchange data
//     if ((send_flags & IBV_SEND_INLINE) == 0) {
//         printf("Registering mr ...");
// 		send_mr = rdma_reg_msgs(id, send_msg, 16);
// 		if (!send_mr) {
// 			ret = -1;
// 			perror("rdma_reg_msgs for send_msg");
// 			goto out_dereg_recv;
// 		}
// 	}
//     print_buffers();

//     // get rkey and mr addr and send to local server
//     my_key = mr->rkey;
//     my_addr = mr->addr;





// out_dereg_send:
// 	if ((send_flags & IBV_SEND_INLINE) == 0)
// 		rdma_dereg_mr(send_mr);
// out_dereg_recv:
// 	// rdma_dereg_mr(mr);
// out_destroy_accept_ep:
// 	rdma_destroy_ep(id);
// out_destroy_listen_ep:
// 	rdma_destroy_ep(listen_id);
// out_free_addrinfo:
// 	rdma_freeaddrinfo(res);
// 	return ret;

// }


// static int local_process () {

// }


int main (int argc, char** argv) {
    int ret, op;
    int is_server = 0;

    server_addr = DEFAULT_SERVER_ADDR;
    port = DEFAULT_PORT;


    while ((op = getopt(argc, argv, "sp:a:")) != -1) {
		switch (op) {
		case 's':
			is_server = 1;
			break;
		case 'p':
			port = optarg;
			break;
        case 'a':
            server_addr = optarg;
            break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-s is_server]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-a server_addr]\n");
			exit(1);
		}
	}

    if (is_server) {
        printf("rdma_server: start\n");
        ret = server_run();
        printf("rdma_server: end %d\n", ret);
    } else {
        printf("rdma_client: start\n");
        ret = client_run();
        printf("rdma_client: end %d\n", ret);
        
    }
}