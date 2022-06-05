# RDMA Terminologies

CAs communicate with each other using work queues. There are three types of work queues: Send, Receive and Completion.

To send or receive messages, Work Requests (WRs) are placed onto a QP.

* wr: work request
    * send wr
    * recieve wr
* wc: work completion
* qp: queue pair for send and recieve message
* cq: completion queue, optionally append a wc to this queue when a work is finished 




https://blog.zhaw.ch/icclab/infiniband-an-introduction-simple-ib-verbs-program-with-rdma-write/

