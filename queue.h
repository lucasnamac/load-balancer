typedef struct queue* queue_ptr;

queue_ptr queue_init();

int queue_is_empty(queue_ptr q);

int queue_insert(queue_ptr q, int server_idx, int client_sock);

int queue_remove(queue_ptr q, int* server_idx, int* client_sock);
