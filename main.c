#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include "queue.h"

#define _MAX_SERVERS 10
#define _MAX_THREAD_NUM 4096
#define _PORT 8080
#define _BACKLOG 4096
#define _BUFFER_SIZE 4100
#define _THREAD_STACK_SIZE 65536

int _NUM_SERVERS;

struct server_info {
    char* address;
    int port;
} servers[_MAX_SERVERS];

pthread_t threads[_MAX_THREAD_NUM];

pthread_mutex_t queue_mutex;

int get_final_server_socket(int server_idx)
{
    int final_server_sock;
    struct sockaddr_in address;
    
    address.sin_family = AF_INET;
    address.sin_port = htons( servers[server_idx].port );

    if(inet_pton(AF_INET, servers[server_idx].address, &address.sin_addr) <= 0) 
    {
        perror("Invalid address / Address not supported");
        exit(EXIT_FAILURE);
    }

    // Creating socket file descriptor 
    if ((final_server_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Failed to initialize final server socket");
        exit(EXIT_FAILURE);
    }

    if (connect(final_server_sock, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Failed to connect file server socket");
        exit(EXIT_FAILURE);
    }

    return final_server_sock;
}

void* proxy_function(void* queue)
{
    int client_sock, server_idx;
    pthread_mutex_lock(&queue_mutex);
    queue_remove(queue, &server_idx, &client_sock);
    pthread_mutex_unlock(&queue_mutex);

    // Read the request content
    ssize_t read_bytes;
    char* buffer = calloc(_BUFFER_SIZE, sizeof(char));

    if ((read_bytes = read(client_sock, buffer, _BUFFER_SIZE - 10)) < 0) {
        perror("Read request content");
        exit(EXIT_FAILURE);
    }

    // Create final server socket
    int final_server_sock = get_final_server_socket(server_idx);

    // Send the request to the final server
    send(final_server_sock, buffer, strlen(buffer), 0);

    // Read the response and proxy it to the client
    while ((read_bytes = read(final_server_sock, buffer, 4096)) > 0) {
        send(client_sock, buffer, read_bytes, 0);
    }
    if (read_bytes < 0) {
        perror("read server response content");
        exit(EXIT_FAILURE);
    }

    printf("### REQUEST FINISHED ###\n");
    shutdown(final_server_sock, SHUT_RDWR);
    close(final_server_sock);
    shutdown(client_sock, SHUT_RDWR);
    close(client_sock);
    int retval = EXIT_SUCCESS;
    pthread_exit(&retval);
}

void servers_entry()
{
    while(1)
    {
        printf("Enter the number of servers (1 <= number_of_servers <= %d): ", _MAX_SERVERS);
        scanf("%d", &_NUM_SERVERS);

        if (_NUM_SERVERS > 0 && _NUM_SERVERS <= _MAX_SERVERS)
        {
            break;
        }
        printf("Invalid input.\n");
    }

    int idx=0;
    while (idx != _NUM_SERVERS)
    {
        printf("Enter the address of the %d server: ", idx+1);
        servers[idx].address = (char*) malloc(20);
        scanf("%s", servers[idx].address);
        printf("Enter the port of the %d server: ", idx+1);
        scanf("%d", &servers[idx].port);
        idx++;
    }
}

void get_next_server_id(int* cur_id)
{
    *cur_id = (*cur_id + 1)%_NUM_SERVERS;
}

pthread_t* get_next_thread(int* iteration){
    int idx = *iteration % _MAX_THREAD_NUM;
    (*iteration) += 1; 

    if (*iteration > _MAX_THREAD_NUM * 5) {
        *iteration -= _MAX_THREAD_NUM;
    }

    if (*iteration < _MAX_THREAD_NUM){
        return &threads[idx];
    } else {
        if (pthread_join(threads[idx], NULL) != 0){
            perror("Failed to join the thread");
            exit(EXIT_FAILURE);
        }
        return &threads[idx];
    }
}

int create_balancer_socket(struct sockaddr_in* address_balancer_sock)
{
    // Struct to handle ipv4 network addresses
    address_balancer_sock->sin_family = AF_INET;
    address_balancer_sock->sin_addr.s_addr = INADDR_ANY;
    address_balancer_sock->sin_port = htons( _PORT );

    // Creating socket file descriptor 
    int balancer_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (balancer_sock == -1) {
        perror("Failed to initialize balancer socket");
        exit(EXIT_FAILURE);
    }
    
    // Set option SO_REUSEADDR at protocol level SOL_SOCKET to option value > 0 (enabled)
    int option_value = 1;
    if (setsockopt(balancer_sock, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value))) {
        perror("Failed to set socket options");
        exit(EXIT_FAILURE);
    }
       
    // Forcefully attaching socket to the defined port
    if (bind(balancer_sock, (struct sockaddr*) address_balancer_sock, sizeof(*address_balancer_sock)) < 0) {
        perror("Failed to bind balancer socket");
        exit(EXIT_FAILURE);
    }

    // Allow the balancer socket to listen and accept incomming connections
    if (listen(balancer_sock, _BACKLOG) < 0) {
        perror("Failed to run listen command on balancer socket");
        exit(EXIT_FAILURE);
    }

    printf("Load balancer listening on port %d\n", _PORT);

    return balancer_sock;
}

int main(int argc, char const *argv[])
{
    queue_ptr connections_queue = queue_init();
    if (connections_queue == NULL) 
    {
        printf("Failed to initialize queue\n");
        exit(EXIT_FAILURE);
    }

    // Receive servers data
    servers_entry();

    struct sockaddr_in address_balancer_sock;
    int balancer_sock = create_balancer_socket(&address_balancer_sock); 

    pthread_attr_t attrs;
    pthread_attr_init(&attrs);

    // Define the stack size for each thread
    pthread_attr_setstacksize(&attrs, _THREAD_STACK_SIZE);

    if(pthread_mutex_init(&queue_mutex, NULL) < 0)
    {
        perror("Failed to init mutex");
        exit(EXIT_FAILURE);
    }

    int client_sock;
    int iteration = 0;
    int target_server = 0;
    while (1)
    {
        struct sockaddr_in address = address_balancer_sock;
        int addrlen = sizeof(address);

        if ((client_sock = accept(balancer_sock, (struct sockaddr*) &address, (socklen_t*) &addrlen)) < 0)
        {
            perror("Failed to accept connection");
            exit(EXIT_FAILURE);
        }

        pthread_mutex_lock(&queue_mutex);
        if (queue_insert(connections_queue, target_server, client_sock) == 0)
        {
            printf("Failed to insert to the queue\n");
            exit(EXIT_FAILURE);
        }
        pthread_mutex_unlock(&queue_mutex);
        pthread_t* thread = get_next_thread(&iteration);
        pthread_create(thread, &attrs, proxy_function, connections_queue);

        get_next_server_id(&target_server);
    }

    return EXIT_SUCCESS;
}
