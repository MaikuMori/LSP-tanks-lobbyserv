#include <inttypes.h>

#include <event2/event.h>

struct server_info {
	//Information about server.
	uint8_t server_name[256];
	uint32_t server_port;

	//Information about players.
	uint8_t players_max;
	uint8_t players_current;
	
	//Information about the current map.
	uint8_t map_name[256];
	uint16_t map_size_x;
	uint16_t map_size_y;
};

struct client {
	//Socket file descriptor.
	int fd;
	//If the client is game server, pointer to server info.
	struct server_info * info;
	//Pointer to servers event loop.
	struct event_base *evloop;
	//Clients I/O event.
	struct bufferevent *buf_event;
	//Address.
	struct sockaddr_in address;
	//Linked list.
	struct client *prev, *next;
};

//Create a new client structure and add it to client list.
struct client * client_new_client(void);
//Free client.
void client_free_client(struct client *c);
//Free all clients.
void client_free_all_clients(void);

//Tail of clients list.
struct client *all_clients;
