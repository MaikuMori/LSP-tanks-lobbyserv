#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <iniparser.h>

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#ifndef CONF_FILE
    #define CONF_FILE "./lts.conf"
#endif

#include "lobby.h"
#include "client.h"

bool debug = false;

static void
readcb(struct bufferevent *bev, void *ctx)
{
    struct client * c = ctx;
    struct evbuffer *input = bufferevent_get_input(bev);
    enum lobby_packet_type packet_id;
    
    struct lobby_packet_empty empty_packet;
    struct lobby_packet_register register_packet;
    struct lobby_packet_update update_packet;
    
    int n;
    
    evbuffer_copyout(input, (void *) &packet_id, sizeof(enum lobby_packet_type));
    
    switch (packet_id) {
        case PING:
            printf("[%s] Got PING packet!\n",
                inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &empty_packet, sizeof(empty_packet));
            break;
        case REGISTER:
            printf("[%s] Got REGISTER packet!\n",
                inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &register_packet, sizeof(register_packet));
            break;
        case UPDATE:
            printf("[%s] Got UPDATE packet!\n",
                inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &update_packet, sizeof(update_packet));
            break;
        case GET_LIST:
            printf("[%s] Got GET_LIST packet!\n",
                inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &empty_packet, sizeof(empty_packet));
            break;
        default:
            printf("[%s] Got unknown packet!\n",
                inet_ntoa(c->address.sin_addr));
            break;
    }
}

static void
eventcb(struct bufferevent *bev, short events, void *ctx)
{
    struct client * c = ctx;
    if (events & BEV_EVENT_ERROR) {
        perror("Error from bufferevent");
    }
    if (events & BEV_EVENT_EOF) {
        printf("[%s] Client closed connection.\n",
             inet_ntoa(c->address.sin_addr));
     }
        
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        client_free_client(c);
    }
}

static void
accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    struct sockaddr_in * addr = (struct sockaddr_in *) address;
    //We got a connection.
    printf("[%s] New connection received.\n",
                inet_ntoa(addr->sin_addr));
    struct client *c = client_new_client();
    
    /* We got a new connection! Set up a bufferevent for it. */
    c->evloop = evconnlistener_get_base(listener);
    c->buf_event = bufferevent_socket_new(
            c->evloop, fd, BEV_OPT_CLOSE_ON_FREE);
    
    memcpy(&(c->address), address, sizeof(struct sockaddr));

    bufferevent_setcb(c->buf_event, readcb, NULL,
                      eventcb, (void *) c);

    bufferevent_enable(c->buf_event, EV_READ|EV_WRITE);
}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    fprintf(stderr, "Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

int
main(int argc, char **argv)
{
    char ch;
    int port, new_port = 0;
    bool daemon = false;
    char *conf_name = NULL;
    pid_t pid, sid;
    struct event_base *base;
    struct evconnlistener *listener;
    struct sockaddr_in sin;
    dictionary *conf_dic;
    
    //Check the input flags.
    while ((ch = getopt(argc, argv, "dvpc:")) != -1) {
        switch (ch) {
            case 'd':
                daemon = true;
                break;
            case 'v':
                debug = true;
                break;
            case 'p':
                new_port = atoi(optarg);
                break;
            case 'c':
                conf_name = optarg;
                break;
        }
    }
    
    if (daemon) {
        pid = fork();
        if (pid < 0) {
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
        umask(0);
        sid = setsid();
        if (sid < 0) {
            exit(EXIT_FAILURE);
        }
    }
    
    //Load configuration file.
    if (conf_name) {
        conf_dic = iniparser_load(conf_name);
    } else {
        conf_dic = iniparser_load(CONF_FILE);
    }
    if (!conf_dic) {
	    err(1, "failed top open config file");
    }
	    	
	if (new_port) {
	    port = new_port;
	} else {
	    port = iniparser_getint(conf_dic, "main:port", 0);
	    if (!port) {
	        err(1, "configuration file doesn't contain item 'port'");
	    }
	}
	
	//Free configuration dictionary.
	iniparser_freedict(conf_dic);
	
	//Make default event base.
	base = event_base_new();
	if (!base) {
		err(1, "open base event failed");
	}
	
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0);
	sin.sin_port = htons(port);

    //Try to bind to the socket.
    listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
        (struct sockaddr*)&sin, sizeof(sin));
    if (!listener) {
            err(1, "create listener failed");
    }
    
    //Set the error callback.
    evconnlistener_set_error_cb(listener, accept_error_cb);
	
	printf("Starting event loop.\n");
	
	//Start the event loop.	
	event_base_dispatch(base);
	
	//Free event base.
	event_base_free(base);
	
	//Free listener (also closes the socket).	
	free(listener);
	
    return (EXIT_SUCCESS);
}
