#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <event2/event.h>
#include <event2/bufferevent.h>

#include "../lobby.h"

//How often to send ping.
static const struct timeval ping_interval = {5,0}; //5 secs.

struct connection {
    int id;
    char reg;
    char auto_reg;
    struct event_base *base;
    struct bufferevent *bev;
    struct event *pingev;
    
    struct connection *next;
    struct connection *prev;
};

//Lists of all connections.
static struct connection *head, *tail;
//Last used connection id.
static int last_id = 0;
//Address where all connections connect to.
static struct sockaddr_in sin;

int connection_new(struct event_base *base, int reg);
void connection_free(struct connection *con);
void connection_free_all(void);
struct connection * connection_get_by_id(int id);

//Gets called when there is something to read from get server connection.
void readcb(struct bufferevent *bev, void *ptr)
{
    struct connection * con = ptr;
    struct lobby_packet_register reg_packet;
    int n;  
    
    n = bufferevent_read(bev, &reg_packet, sizeof(reg_packet));
    if (n == sizeof(reg_packet)) {
        printf("-> Client %d: Got reg_packet back! Name: %s\n", con->id, reg_packet.server_name);
    } else {
        
        //printf("-> Client %d: Didn't get full packet back (%d).\n", con->id, n);
    }
}

//Gets called each time it's time to ping the server.
void pingcb(evutil_socket_t fd, short events, void *ptr)
{
    int r;
    struct connection * con = ptr;
    struct lobby_packet_empty ping_packet;
    
    ping_packet.packet_id = PING;
    
    r = bufferevent_write(con->bev, (const void *) &ping_packet, sizeof(ping_packet));
    if(r) {
        printf("Clinet %d: Failed to send PING!\n", con->id);
    }
}

//Gets called when there is something to read from get list connection.
void list_readcb(struct bufferevent *bev, void *ptr)
{
    struct lobby_packet_list list_packet;
    struct lobby_list_item item;
    int n;  
    
    n = bufferevent_read(bev, &list_packet, sizeof(list_packet));
    if (n == sizeof(list_packet)) {
        printf("-> list: Got list_packet!\n");
    } else {
        printf("-> list: list_packet has wrong size!");
    }
    bufferevent_free(bev);
}

//General event callback for all server connections.
void eventcb(struct bufferevent *bev, short events, void *ptr)
{
    int r;
    struct connection * con = ptr;
    struct lobby_packet_register reg_packet;
    
    if (events & BEV_EVENT_CONNECTED) {
        printf("-> Client %d: connected!\n", con->id);
        if (con->auto_reg) {
            //Fill the REGISTER packet.
            reg_packet.packet_id = REGISTER;
            reg_packet.server_port = 3133 + con->id;
            strncpy((char *) reg_packet.server_name, "Test Server ", 256);
            snprintf((char *)&(reg_packet.server_name[12]), 256 - 12, "%d", con->id);
            
            //Send it.
            printf("-> Client %d: Sending register packet ... ", con->id);
            r = bufferevent_write(con->bev, (const void *) &reg_packet, sizeof(reg_packet));
            if(r) {
                printf("failed!\n");
            } else {
                printf("done!\n");
                con->reg = 1;
            }
        
            //Activate ping event.
            event_add(con->pingev, &ping_interval);
        }
    } else if (events & BEV_EVENT_ERROR) {
        printf("-> Client %d: failed to connect!\n", con->id);
    }
    if (events & BEV_EVENT_EOF) {
        printf("-> Client %d: Server closed connection.\n", con->id);
    }
}

//General event callback for get list connection.
void list_eventcb(struct bufferevent *bev, short events, void *ptr)
{
    int r;
    struct lobby_packet_empty get_list_packet;
    
    if (events & BEV_EVENT_CONNECTED) {
        printf("-> list: connection made!\n");
        get_list_packet.packet_id = GET_LIST;
        //Send it.
        printf("-> list: Sending GET_LIST packet ... ");
        r = bufferevent_write(bev, (const void *) &get_list_packet, sizeof(get_list_packet));
        if(r) {
            printf("failed!\n");
        } else {
            printf("done!\n");
        }  
    } else if (events & BEV_EVENT_ERROR) {
        printf("-> list: failed to connect!\n");
    }
    if (events & BEV_EVENT_EOF) {
        printf("-> list: Server closed connection.\n");
    }
}

//Handle user input from stdin.
void input_handler(struct bufferevent *bev, void *ptr)
{
    struct event_base *base = ptr;
    struct bufferevent *list_bev;
    struct lobby_packet_register reg_packet;
    struct lobby_packet_empty empty_packet;
    struct connection *con = NULL;
    char buffer[256] = {0};
    char * pch;
    int n, r, id;
    unsigned int port;
    n = bufferevent_read(bev, &buffer, 256);
    buffer[255] = '\0';
    
    //Get get command name.
    pch = strtok(buffer, " \n");
    
    //Try to match the command agains't the list of supported commands.
    if (!strncmp(pch, "exit", n)) {
        printf("Closing down ... ");
        connection_free_all();
        bufferevent_free(bev);
        event_base_loopexit(base, NULL);
    } else if (!strncmp(pch, "add", n)) {
        //Check if we want to disable auto register. On by default.
        pch = strtok(NULL, " \n");
        if (!pch) {
            connection_new(base, 1);
        } else if (atoi(pch) == 0) {
            connection_new(base, 0);
        } else {
            connection_new(base, 1);
        }    
    } else if (!strncmp(pch, "mass_add", n)) {
        //Get number of servers to add.
        pch = strtok(NULL, " \n");
        if (!pch) {
            printf("mass_add: not enough parameters.\n");
            return;
        }
        r = atoi(pch);
        while (r--){
            connection_new(base, 1);
        }
    } else if (!strncmp(pch, "close", n)) {
        pch = strtok(NULL, " \n");
        if (!pch) {
            printf("register: not enough parameters.\n");
            return;
        }
        id = atoi(pch);
        con = connection_get_by_id(id);
        if (!con) {
            printf("register: no client with that id (%d).\n", id);
            return;
        }
        printf("-> Client %d: Closing connection ...", con->id);
        connection_free(con);
        printf("done!\n");
    } else if (!strncmp(pch, "register", n)) {
        //Find the client by id.
        pch = strtok(NULL, " \n");
        if (!pch) {
            printf("register: not enough parameters.\n");
            return;
        }
        id = atoi(pch);
        con = connection_get_by_id(id);
        if (!con) {
            printf("register: no client with that id (%d).\n", id);
            return;
        }
        //Fill the register packet.
        reg_packet.packet_id = REGISTER;
        pch = strtok(NULL, " \n");
        if (!pch) {
            printf("register: not enough parameters.\n");
            return;
        }
        reg_packet.server_port = atoi(pch);
        pch = strtok(NULL, "\n");
        if (!pch) {
            printf("register: not enough parameters.\n");
            return;
        }
        strncpy((char *) reg_packet.server_name, pch, 256);
        //Send it.
        printf("-> Client %d: Sending register packet ... ", con->id);
        r = bufferevent_write(con->bev, (const void *) &reg_packet, sizeof(reg_packet));
        if(r) {
            printf("failed!\n");
        } else {
            printf("done!\n");
            con->reg = 1;
        }
        
        //Activate ping event.
        event_add(con->pingev, &ping_interval);

    } else if (!strncmp(pch, "list", n)) {
        list_bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
        //Set callback and enable read and write on socket bufferevent.
        bufferevent_setcb(list_bev, list_readcb, NULL, list_eventcb, NULL);
        bufferevent_enable(list_bev, EV_READ|EV_WRITE);
        
        printf("-> Connecting to localhost ... ");
        if (bufferevent_socket_connect(list_bev, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            printf("failed!\n");
            bufferevent_free(list_bev);
            return;
        }
        printf("done!\n");
    } else {
        printf("Unknown command.\n");
    }
}

int main(void) {
    struct event_base *base;
    struct bufferevent * inputev;
    
    base = event_base_new();
    
    //Setup the input handler event.
    inputev = bufferevent_socket_new(base, 0, 0);
    bufferevent_setcb(inputev, input_handler, NULL, NULL, base);
    bufferevent_enable(inputev, EV_READ);

    //Set up the address where all connections connect to.
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0x7f000001); /* 127.0.0.1 */
    sin.sin_port = htons(3122);
    
    //Print some help.
    printf("Lobby Server Tester. Available commands:\n");
    printf(" %-40s Starts a new server connection.\n", "add register");
    printf(" %-40s Starts several new server connections.\n", "mass_add count");
    printf(" %-40s Close a specific server connections\n", "close client_id");
    printf(" %-40s Manually register server.\n", "register client_id port server_name");
    printf(" %-40s Manually update server. (not implemented)\n", "update ...");
    printf(" %-40s Request server list.\n", "list");
    printf(" %-40s Exit Lobby Server Tester.\n", "exit");
    
    //Start up the main event base loop.
    event_base_dispatch(base);
    //We're done, free the event base.
    event_base_free(base);
    printf("done!\n");
    
    return 0;
}

//Create a new server connection.
int connection_new(struct event_base *base, int reg) {
    struct connection * con = (struct connection *) malloc(sizeof(struct connection));
    
    con->id = ++last_id;
    con->reg = 0;
    con->auto_reg = reg;
    con->base = base;
    con->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
    con->pingev = event_new(base, -1, EV_PERSIST, pingcb, con);
        
    //Set callback and enable read and write on socket bufferevent.
    bufferevent_setcb(con->bev, readcb, NULL, eventcb, con);
    bufferevent_enable(con->bev, EV_READ|EV_WRITE);
    
    printf("-> Client id is %d.\n", con->id);
    printf("-> Connecting to localhost ... ");
    if (bufferevent_socket_connect(con->bev, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        printf("failed!\n");
        bufferevent_free(con->bev);
        free(con);
        return -1;
    }
    printf("done!\n");
    
    if(head == NULL) {
        head = con;
        con->prev = NULL;
    } else {
        tail->next = con;
        con->prev = tail;
    }
    tail = con;
    con->next = NULL;
    
    return 0;
}

//Free a single connection.
void connection_free(struct connection *con) {
    if(con->prev == NULL) {
        head = con->next;
    } else {
        con->prev->next = con->next;
    }
    if(con->next == NULL) {
        tail = con->prev;
    } else {
        con->next->prev = con->prev;
    }
    
    if (con->bev) {
        bufferevent_free(con->bev);
    }
    event_del(con->pingev);
    event_free(con->pingev);
    free(con);
}

//Free all connections and the underlaying events.
void connection_free_all(void) {
    struct connection *temp = tail;
    
    while(temp) {
        temp = tail->prev;
        
        if (tail->bev) {
            bufferevent_free(tail->bev);
        }
        event_del(tail->pingev);
        event_free(tail->pingev);
        free(tail);
        tail = temp;
    }
}

//Find connection by it's ID.
struct connection * connection_get_by_id(int id) {
    struct connection *temp = tail;
    
    while(temp) {
        if (temp->id == id) {
            break;
        }
        temp = temp->prev;
    }
    
    return temp;
}
