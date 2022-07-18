/* 
 * Compile Command:
 * g++ rdma_sandbox.cc -o rdma_sandbox.exe -libverbs -lrdmacm -pthread
 * 
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
#include <infiniband/verbs.h>

int main () {

    static struct rdma_cm_id *listen_id, *id;
    struct rdma_addrinfo hints, *res;
	struct ibv_qp_init_attr init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_wc wc;
    struct ibv_device_attr device_attr;
	int ret;


	struct ibv_context* context = NULL;
    int num_devices;
    struct ibv_device** device_list = ibv_get_device_list(&num_devices);
    printf("found %d device(s)\n", num_devices);
    // for (int i = 0; i < num_devices; i++){
    //     /* match device name. open the device and return it */
    //     printf("%s\n", ibv_get_device_name(device_list[i]));
    //     if (strcmp(device_name, ibv_get_device_name(device_list[i])) == 0) {
            
    //         break;
    //     }
    // }
    context = ibv_open_device(device_list[0]);
    /* it is important to free the device list; otherwise memory will be leaked. */
    ibv_free_device_list(device_list);
    if (context == NULL) {
        fprintf(stderr, "Unable to find the device ");
        // std::cerr << "Unable to find the device " << device_name << std::endl;
    }

    ret = ibv_query_device(context, &device_attr);
    if (ret) {
        return -1;
    }

    printf("max_mr_size: %lu\n", device_attr.max_mr_size);

out_free_addrinfo:
	rdma_freeaddrinfo(res);
	return ret;
}