#include <inttypes.h>

#include <event2/event.h>

struct client {
	//Socket file descriptor.
	int fd;
	//Pointer to servers event loop.
	struct event_base *evloop;
	//Clients I/O event.
	struct bufferevent *buf_event;
	//Output buffer.
	struct evbuffer *buffer;
	//Linked list.
	struct client *prev, *next;
};

struct client * client_new_client(void);
void client_free_client(struct client *c);
void client_free_all_clients(void);
