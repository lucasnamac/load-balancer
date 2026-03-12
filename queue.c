#include <stdio.h>
#include <stdlib.h>
#include "queue.h"


struct no {
    int server_idx;
    int client_sock;
    struct no* prox;
};

struct queue{
    struct no* ini;
    struct no* fim;
};

queue_ptr queue_init(){
    queue_ptr q;
    q = (queue_ptr) malloc(sizeof(struct queue));
    if (q != NULL) {
        q->ini = NULL;
        q->fim = NULL;
    }
    return q;
}

int queue_is_empty(queue_ptr q){
    if (q->ini == NULL)
        return 1;
    return 0;
}

int queue_insert(queue_ptr q, int server_idx, int client_sock) {
    struct no *node;
    node = (struct no *) malloc(sizeof(struct no));
    if (node == NULL) return 0;

    node->server_idx = server_idx;
    node->client_sock = client_sock;
    node->prox = NULL;

    if (queue_is_empty(q) == 1)
        q->ini = node;
    else
        (q->fim)->prox = node;

    q->fim = node;
    return 1;
}

int queue_remove(queue_ptr q, int *server_idx, int *client_sock) {
    if (queue_is_empty(q) == 1)
        return 0;

    struct no *aux = q->ini;
    *server_idx = aux->server_idx;
    *client_sock = aux->client_sock;

    if (q->ini == q->fim)
        q->fim = NULL;

    q->ini = aux->prox;
    free(aux);
    return 1;
}
