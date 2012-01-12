/*
    Default workflow for game server:
    1)Connect to lobby server.
    2)Receive INFO packet from lobby server.
    3)Send REGISTER packet.
    4)Optionally send UPDATE packet to give lobby server more
      information.
    5)Each LOBBY_SEND_INTERVAL seconds send PING packet if nothing
      changed or UPDATE packet if something changed.
    
    Default workflow for game client updating sever list:
    1)Connect to lobby server.
    2)Receive INFO packet from lobby server.
    3)Send GET_LIST packet.
    4)Either disconnect or each LOBBY_SEND_INTERVAL seconds
      send GET_LIST packet again.
    
     Some sort of subscription model where only updated information is
    sent to game client could also be implemented if needed.
    
    For more info check out test_client.c
*/

#include <inttypes.h>

//Lobby server clients should send data at least LOBBY_SEND_INTERVAL
//seconds or they will be timed out.
#define LOBBY_SEND_INTERVAL 15
//Default string len.
#define LOBBY_STR_LEN 256

//Used for 'packet_id'.
enum __attribute__((packed)) lobby_packet_type {
    PING,
    REGISTER,
    UPDATE,
    GET_LIST,
    LIST,
    INFO
};

//PING packet or GET_LIST packet.
//Just set the correct packet_id and send.
struct lobby_packet_empty {
    enum lobby_packet_type packet_id;
};

//REGITER packet. Registers server in the lobby list. Has bare minimum
//information.
struct __attribute__((packed)) lobby_packet_register {
    enum lobby_packet_type packet_id;
    
    //Information about server.
    uint8_t server_name[LOBBY_STR_LEN];
    uint32_t server_port;
};

//UPDATE packet. Contains non-constant information which might change
//with time.
struct __attribute__((packed)) lobby_packet_update {
    enum lobby_packet_type packet_id;
        
    //Information about players.
    uint8_t players_max;
    uint8_t players_current;
    
    //Information about the current map.
    uint8_t map_name[LOBBY_STR_LEN];
    uint16_t map_size_x;
    uint16_t map_size_y;
};

//List item. Each LIST packet has 0 or n list items.
struct lobby_list_item {
    //Information about server.
    uint8_t server_name[LOBBY_STR_LEN];
    uint8_t server_ip[4]; //Probbly will change to struct sockaddr_in
    uint32_t server_port;

    //Information about players.
    uint8_t players_max;
    uint8_t players_current;
    
    //Information about the current map.
    uint8_t map_name[LOBBY_STR_LEN];
    uint16_t map_size_x;
    uint16_t map_size_y;
};

//LIST packet. Contains all registered servers and their information.
struct __attribute__((packed)) lobby_packet_list {
    enum lobby_packet_type packet_id;
        
    uint8_t item_count;
    struct lobby_list_item items[];
};

//INFO packet. Sent by lobby server when new connection is made.
struct __attribute__((packed)) lobby_packet_info {
    enum lobby_packet_type packet_id;
    
    //Information about lobby server.
    uint8_t ver_major;
    uint8_t ver_minor;
};
