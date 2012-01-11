/*
	
*/

//Used for 'packet_id'.
enum __attribute__((packed)) lobby_packet_type {
	PING,
	REGISTER,
	UPDATE,
	GET_LIST,
	LIST
};

//Ping packet.
struct lobby_packet_empty {
	enum lobby_packet_type packet_id;
};

//Add server to the list packet. Bare minimum information.
struct __attribute__((packed)) lobby_packet_register {
	enum lobby_packet_type packet_id;
	
	//Information about server.
	uint8_t server_name[255];
	uint32_t server_port;
};

//Update packet.
struct __attribute__((packed)) lobby_packet_update {
	enum lobby_packet_type packet_id;
		
	//Information about players.
	uint8_t players_max;
	uint8_t players_current;
	
	//Information about the current map.
	uint8_t map_name[255];
	uint16_t map_size_x;
	uint16_t map_size_y;
};

//List item.
struct lobby_list_item {
	//Information about server.
	uint8_t server_name[255];
	uint8_t server_ip[4];
	uint32_t server_port;

	//Information about players.
	uint8_t players_max;
	uint8_t players_current;
	
	//Information about the current map.
	uint8_t map_name[255];
	uint16_t map_size_x;
	uint16_t map_size_y;
};

//List packet.
struct __attribute__((packed)) lobby_packet_list {
	enum lobby_packet_type packet_id;
		
	uint8_t item_count;
	struct lobby_list_item items[];
};
