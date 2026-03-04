/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here

# Student #1: Kevin
# Student #2:
# Student #3: 

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

typedef struct {
    int epoll_fd;
    int socket_fd;
} client_thread_data_t;

void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];

    int window_size = 10;
    int base = 0;
    int next_seq = 0;

    long tx_cnt = 0;
    long rx_cnt = 0;

    // Register socket with epoll
    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event);

    while (rx_cnt < num_requests) {

        // Send while window not full
        while (next_seq < base + window_size && next_seq < num_requests) {

            char send_buf[MESSAGE_SIZE];
            memset(send_buf, 'A', MESSAGE_SIZE);

            if (send(data->socket_fd, send_buf, MESSAGE_SIZE, 0) < 0) {
                perror("send failed");
                break;
            }

            tx_cnt++;
            next_seq++;
        }

        // Wait for replies
        int n_events = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);

        if (n_events < 0) {
            perror("epoll_wait failed");
            break;
        }

        for (int i = 0; i < n_events; i++) {
            if (events[i].data.fd == data->socket_fd) {

                char recv_buf[MESSAGE_SIZE];

                if (recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0) > 0) {
                    rx_cnt++;
                    base++;
                }
            }
        }
    }

    long lost_pkt = tx_cnt - rx_cnt;

    printf("Thread finished\n");
    printf("tx_cnt = %ld\n", tx_cnt);
    printf("rx_cnt = %ld\n", rx_cnt);
    printf("lost packets = %ld\n", lost_pkt);

    close(data->socket_fd);
    close(data->epoll_fd);

    return NULL;
}

void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Create client threads
    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].epoll_fd = epoll_create1(0);
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // change to SOCK_DGRAM for UDP
        connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    }

    for (int i = 0; i < num_client_threads; i++) {
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }
    
    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Client finished.\n");
}

void run_server() {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0); // change to SOCK_DGRAM for UDP
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    // listen(server_fd, 128); // remove listen for UDP

    int epoll_fd = epoll_create1(0);
    struct epoll_event event, events[MAX_EVENTS];

    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < n_events; i++) {

            if (events[i].data.fd == server_fd) {

                char buffer[MESSAGE_SIZE];
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                int n = recvfrom(server_fd, buffer, MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);

                if (n > 0) {
                    sendto(server_fd, buffer, MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, addr_len);
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
