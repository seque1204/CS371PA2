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
# Student #1: Ty Eubanks
# Student #2: Jose Sequeira
# Student #3: 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MESSAGE_SIZE 16
#define PAYLOAD_SIZE (MESSAGE_SIZE - sizeof(int))
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

typedef struct {
    int seq_num;
    char payload[PAYLOAD_SIZE];
} packet_t;

typedef struct {
    int socket_fd;
    struct sockaddr_in server_addr;
    long tx_cnt;
    long rx_cnt;
    long long total_rtt;
    long total_messages;
    int requests_per_thread;
    long retransmissions;
} client_thread_data_t;

void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    packet_t packet;
    packet_t ack;
    socklen_t addrlen = sizeof(data->server_addr);
    struct timeval start, end;
    int seq_num = 0;

    for (int i = 0; i < data->requests_per_thread; i++) {
        int attempts = 1;
        bool success = false;
    
        memcpy(packet.payload, "ABCDEFGHIJKMLNOP", PAYLOAD_SIZE);
    
        while (!success) {
            struct timeval timeout = {1, 0};
            packet.seq_num = seq_num;
    
            gettimeofday(&start, NULL);
            if (sendto(data->socket_fd, &packet, sizeof(packet), 0,
                       (struct sockaddr *)&data->server_addr, addrlen) != sizeof(packet)) {
                perror("sendto");
                attempts++;
                continue;
            }
    
            data->tx_cnt++; // Count each send attempt (including retries)
    
            fd_set read_fds; 
            FD_ZERO(&read_fds);
            FD_SET(data->socket_fd, &read_fds);
            int ret = select(data->socket_fd + 1, &read_fds, NULL, NULL, &timeout);
    
            if (ret > 0) {  // A response is ready to be read
                ssize_t bytes = recvfrom(data->socket_fd, &ack, sizeof(ack), 0, NULL, NULL);
                if (bytes == sizeof(ack) && ack.seq_num == seq_num) {
                    gettimeofday(&end, NULL);
                    long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
                    data->total_rtt += rtt;
                    data->total_messages++;
                    data->rx_cnt++;
                    success = true;
                    seq_num++;
                } else {
                    // If an unexpected packet is received, count this as a failed attempt.
                    attempts++;
                }
            } else {  
                // If select times out or encounters an error, count the attempt.
                attempts++;
            }
        }
    
        // Count retransmissions (all attempts beyond the first one).
        data->retransmissions += (attempts > 1) ? (attempts - 1) : 0;
    }
    close(data->socket_fd);
    return NULL;
}

void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];

    int base_requests = num_requests / num_client_threads;
    int remainder = num_requests % num_client_threads;

    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].requests_per_thread = base_requests + (i < remainder ? 1 : 0);

        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (thread_data[i].socket_fd == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        thread_data[i].server_addr.sin_family = AF_INET;
        thread_data[i].server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip, &thread_data[i].server_addr.sin_addr);

        thread_data[i].tx_cnt = 0;
        thread_data[i].rx_cnt = 0;
        thread_data[i].total_rtt = 0;
        thread_data[i].total_messages = 0;
        thread_data[i].retransmissions = 0;

        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    long long total_rtt = 0;
    long total_messages = 0;
    long total_tx = 0;
    long total_rx = 0;
    long total_retransmissions = 0;

    // Wait for all threads to finish
    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_tx += thread_data[i].tx_cnt;
        total_rx += thread_data[i].rx_cnt;
        total_retransmissions += thread_data[i].retransmissions;
    }

    double total_request_rate = (double)total_messages / ((double)total_rtt / 1000000.0);
    
    // Print summary
    printf("\n=== Client Summary ===\n");
    printf("Total Sent: %ld\n", total_tx);
    printf("Total Received: %ld\n", total_rx);
    printf("Total Lost: %ld\n", total_tx - total_rx);
    printf("Lost (percentage): %.2f%%\n",
           (double)(total_tx - total_rx) / total_tx * 100);
    printf("Average RTT: %lld us\n", total_messages ? total_rtt / total_messages : 0);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
    printf("Total Retransmissions: %ld\n", total_retransmissions);
}

void run_server() {
    int sock_fd;
    struct sockaddr_in server_addr, client_addr;
    packet_t packet;
    socklen_t addrlen = sizeof(client_addr);

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        ssize_t bytes = recvfrom(sock_fd, &packet, sizeof(packet), 0,
                                 (struct sockaddr *)&client_addr, &addrlen);
        if (bytes == sizeof(packet)) {
            usleep(5000); // Simulate processing delay
            sendto(sock_fd, &packet, sizeof(packet), 0,
                   (struct sockaddr *)&client_addr, addrlen);
        }
    }

    close(sock_fd);
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
