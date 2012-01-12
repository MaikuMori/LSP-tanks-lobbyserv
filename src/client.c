#include <stdlib.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include "client.h"

static struct client *all_clients;

struct client *
client_new_client(void)
{
    struct client * c = calloc(1, sizeof(struct client));
    
    if (all_clients) {
        c->prev = all_clients;
        all_clients->next = c;
    }
    c->next = NULL;
    all_clients = c;
    
    return c;
}

void
client_free_client(struct client *c)
{
    if (c->next != NULL) {
        c->next->prev = c->prev;
    } else {
        all_clients = c->prev;
    }

    if (c->prev != NULL) {
        c->prev->next = c->next;
    }
    
    if (c->buf_event) {
        bufferevent_free(c->buf_event);
    }

    free(c);
}

void
client_free_all_clients(void)
{
    struct client *temp = all_clients;
    
    while(temp) {
        temp = all_clients->prev;
        
        if (all_clients->buf_event) {
            bufferevent_free(all_clients->buf_event);
        }
        
        free(all_clients);
        
        all_clients = temp;
    }
}
