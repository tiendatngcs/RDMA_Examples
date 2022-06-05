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

void createContext(void) {
    /* There is no way to directly open the device with its name; we should get the list of devices first. */
    struct ibv_context* context = NULL;
    int num_devices;
    struct ibv_device** device_list = ibv_get_device_list(&num_devices);
    printf("Number of detected devices %d\n", num_devices);
    for (int i = 0; i < num_devices; i++){
        printf("%s\n", ibv_get_device_name(device_list[i]));
    /* match device name. open the device and return it */
    // if (device_name.compare(ibv_get_device_name(device_list[i])) == 0) {
    //   context = ibv_open_device(device_list[i]);
    //   break;
    // }
    }

  /* it is important to free the device list; otherwise memory will be leaked. */
//   ibv_free_device_list(device_list);
//   if (context == nullptr) {
//     std::cerr << "Unable to find the device " << device_name << std::endl;
//   }
//   return context;
}

void get_info_and_create_ep(char* server_name, char* server_port) {
    int ret;
    struct rdma_addrinfo hints, *res;

    ret = rdma_getaddrinfo(server_name, server_port, NULL, &res);
    if (ret) {
        VERB_ERR("rdma_getaddrinfo", ret);
        return ret;
    }

    // struct rdma_cm_id** connection_manager_id_list; 
    // rdma_create_ep(connection_manager_id_list, );

    rdma_freeaddrinfo(res);

}


int main (int argc, char** argv) {
    get_info_and_create_ep(NULL, );

}