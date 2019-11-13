/* Mesh Internal Communication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
/**
 * Lib C
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/**
 * FreeRTOS
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "mesh_light.h"
#include "nvs_flash.h"

/**
 * Lwip
 */
#include "lwip/err.h"
#include "lwip/sys.h"
#include <lwip/sockets.h>

 /**
 * Lib MQTT
 */
#include "mqtt_client.h"

 /**
 * cJSON
 */
#include "cJSON.h"

/*******************************************************
 *                Macros
 *******************************************************/
//#define MESH_P2P_TOS_OFF
//#define  MESH_FIX_ROOT
/*******************************************************
 *                Constants
 *******************************************************/
#define RX_SIZE          (1500)
#define TX_SIZE          (1460)

char mac_address_root_str[50];
esp_mqtt_client_handle_t client;
int processJson( char * mb_request );

/*******************************************************
 *                Variable Definitions
 *******************************************************/
 static uint8_t hello_buf[TX_SIZE] = { 0x3E,0x3E, 0x48,0x65,0x6c,0x6c,0x6f,0x5f,0x4d,0x65,0x73,0x68,0x21, 0x00}; // ">>Hello,Mesh!" in hexa
 static bool gotIP = false;
 static const char *TAG = "main: ";
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = { 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static uint8_t tx_buf[TX_SIZE] = { 0, };
static uint8_t rx_buf[RX_SIZE] = { 0, };
static bool is_running = true;
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;

mesh_light_ctl_t light_on = {
    .cmd = MESH_CONTROL_CMD,
    .on = 1,
    .token_id = MESH_TOKEN_ID,
    .token_value = MESH_TOKEN_VALUE,
};

mesh_light_ctl_t light_off = {
    .cmd = MESH_CONTROL_CMD,
    .on = 0,
    .token_id = MESH_TOKEN_ID,
    .token_value = MESH_TOKEN_VALUE,
};

#define SERVER_IP 	"10.1.1.4"
#define SERVER_PORT 9765
#define DEBUG 1

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

/**
 * 1-Configura o ESP32 em modo Socket Client;
 * 2-Conecta ao servidor TCP; 
 * 3-Envia string para o servidor; 
 * 4-Encerra a conexão socket com o servidor;
 */
void socket_client( int count ) 
{
    int rc; 
    char str[50];
	
	if( DEBUG )
		ESP_LOGI(TAG, "socket_client...\n");

	/**
	 * Cria o socket TCP;
	 */
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if( DEBUG )
		ESP_LOGI( TAG, "socket: rc: %d", sock );

	/**
	 * Configura/Vincula o endereço IP e Port do Servidor (Bind);
	 */
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr.s_addr);
	serverAddress.sin_port = htons(SERVER_PORT);

	/**
	 * Tenta estabelecer a conexão socket TCP entre ESP32 e Servidor;
	 */
	rc = connect(sock, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in));
	
	/**
	 * error? (-1)
	 */
	if( DEBUG )
		ESP_LOGI(TAG, "connect rc: %d", rc);

	if( rc == 0 )
	{
		/**
		 * Converte o valor de count para string e envia a string 
		 * ao servidor via socket tcp;
		 */
		sprintf( str, "Count = %d\r\n", count );
		rc = send( sock, str, strlen(str), 0 );
		/**
		 * error durante o envio? (-1)
		 */
		if( DEBUG )
			ESP_LOGI(TAG, "send: rc: %d", rc);		
	}

	rc = close( sock );
	/**
	 * error no fechamento do socket? (-1)
	 */
	if( DEBUG )
		ESP_LOGI(TAG, "close: rc: %d", rc);

}


/*******************************************************
 *                Function Declarations
 *******************************************************/
void send2Server(mesh_addr_t* mAddr,mesh_data_t* mData){
    int count = 0;
    if(!gotIP) return;

    ESP_LOGE(MESH_TAG, "Send to server");
    //esp_mesh_send(mAddr, mData, MESH_DATA_TODS, NULL , 0);
    socket_client(count++);
}
/*******************************************************
 *                Function Definitions
 *******************************************************/
static int route_table_size = 0;
static mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
void esp_mesh_p2p_tx_main(void *arg)
{
    int i;
    esp_err_t err;
    int send_count = 0;
    
    
    mesh_data_t data;
    
        mesh_addr_t serverAddr;
    mesh_data_t serverData;
    
    
    data.data = tx_buf;
    data.size = sizeof(tx_buf);
    data.proto = MESH_PROTO_BIN;
#ifdef MESH_P2P_TOS_OFF
    data.tos = MESH_TOS_DEF;
#endif /* MESH_P2P_TOS_OFF */

    serverData.data = hello_buf;
    serverData.size = sizeof(hello_buf);
    serverData.proto = MESH_PROTO_BIN;
    
    IP4_ADDR(&serverAddr.mip.ip4, 10,1,1,4);
    serverAddr.mip.port = 8089;
    
    is_running = true;
    while (is_running) {
        
                send2Server(&serverAddr, &serverData);
      

        ESP_LOGE(MESH_TAG, "Routing table");
        for (i = 0; i < route_table_size; i++) {
            ESP_LOGE(MESH_TAG,MACSTR,MAC2STR(route_table[i].addr));
        }

        
        /* non-root do nothing but print */
        if (!esp_mesh_is_root()) {
            ESP_LOGI(MESH_TAG, "layer:%d, rtableSize:%d, %s", mesh_layer,
                     esp_mesh_get_routing_table_size(),
                     (is_mesh_connected && esp_mesh_is_root()) ? "ROOT" : is_mesh_connected ? "NODE" : "DISCONNECT");
            vTaskDelay(10 * 1000 / portTICK_RATE_MS);
            
            //char* txt = "HELLO"; 
            //err = esp_mesh_send(NULL, txt, 0, NULL, 0);
            
            continue;
        }
        esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
        if (send_count && !(send_count % 100)) {
            ESP_LOGI(MESH_TAG, "size:%d/%d,send_count:%d", route_table_size,
                     esp_mesh_get_routing_table_size(), send_count);
        }
        send_count++;
        tx_buf[25] = (send_count >> 24) & 0xff;
        tx_buf[24] = (send_count >> 16) & 0xff;
        tx_buf[23] = (send_count >> 8) & 0xff;
        tx_buf[22] = (send_count >> 0) & 0xff;
        if (send_count % 2) {
            memcpy(tx_buf, (uint8_t *)&light_on, sizeof(light_on));
        } else {
            memcpy(tx_buf, (uint8_t *)&light_off, sizeof(light_off));
        }

        for (i = 0; i < route_table_size; i++) {
            err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
            if (err) {
                ESP_LOGE(MESH_TAG,
                         "[ROOT-2-UNICAST:%d][L:%d]parent:"MACSTR" to "MACSTR", heap:%d[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_free_heap_size(),
                         err, data.proto, data.tos);
            } else if (!(send_count % 100)) {
                ESP_LOGW(MESH_TAG,
                         "[ROOT-2-UNICAST:%d][L:%d][rtableSize:%d]parent:"MACSTR" to "MACSTR", heap:%d[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_layer,
                         esp_mesh_get_routing_table_size(),
                         MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_free_heap_size(),
                         err, data.proto, data.tos);
            }
        }
        /* if route_table_size is less than 10, add delay to avoid watchdog in this task. */
        if (route_table_size < 10) {
            vTaskDelay(1 * 1000 / portTICK_RATE_MS);
        }
    }
    vTaskDelete(NULL);
}

    char buffer_s[100];
    mesh_data_t data_s;
void esp_mesh_p2p_rx_main(void *arg)
{
    int recv_count = 0;
    esp_err_t err;
    mesh_addr_t from;
    int send_count = 0;
    mesh_data_t data;
    int flag = 0;
    data.data = rx_buf;
    data.size = RX_SIZE;
    char buffer[100];
    char mac_str[50];
    data_s.data = (uint8_t*)buffer_s;
    data_s.size = sizeof(buffer_s);

    is_running = true;
    while (is_running) {
        data.size = RX_SIZE;
        err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err != ESP_OK || !data.size) {
            ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
            continue;
        }

        if (!esp_mesh_is_root()) 
        {

            if(strcmp((char*)data.data, "hello")==0)
            {
                mesh_light_process(&from, data.data, data.size);

                ESP_LOGI( TAG, "slave - processa hello" ); 
                sprintf((char*)buffer_s, "\"temp\":%d,\"mac\":\""MACSTR"\"", esp_random(), MAC2STR(mesh_parent_addr.addr));

                err = esp_mesh_send(NULL, &data_s, 0, NULL, 0);
                if (err != ESP_OK ) {
                    ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data_s.size);
                }                
            }


        }else
////{"mb_reset":1,"mqtt_publish_in":"24:0a:c4:96:46:98"}
////{"mb_reset":1}
////{"mqtt_publish_in":"24:0a:c4:96:46:98"}
       // ESP_LOGW(MESH_TAG, "[MSG: %s ] parent:"MACSTR", receive from "MACSTR" ", data.data, MAC2STR(mesh_parent_addr.addr), MAC2STR(from.addr) );
        
        if (esp_mesh_is_root()) 
        {
            ESP_LOGI( TAG, "server - prepara json" ); 

            strcpy( buffer, "{\"device\":\"" );
            sprintf(mac_str, MACSTR, MAC2STR(from.addr) );
            strcat(  buffer, mac_str);
            strcat(  buffer, "\",");
            strcat(  buffer, (char*)data.data );
            strcat(  buffer, "}");

            if(esp_mqtt_client_publish(client, "senai", buffer, strlen(buffer), 0, 0) == -1)
            {
                if( DEBUG )
                    ESP_LOGI( TAG, "%s", "Error durante o envio via mqtt.\r\n" ); 
            } 
                          
         }
        /* extract send count */
        //if (data.size >= sizeof(send_count)) {
            //send_count = (data.data[25] << 24) | (data.data[24] << 16)
                        // | (data.data[23] << 8) | data.data[22];
        //}
        //recv_count++;
        /* process light control */
       // mesh_light_process(&from, data.data, data.size);
        

        /*
       if (!(recv_count % 1)) {
            ESP_LOGW(MESH_TAG,
                     "[#RX:%d/%d][L:%d] parent:"MACSTR", receive from "MACSTR", size:%d, heap:%d, flag:%d[err:0x%x, proto:%d, tos:%d]",
                     recv_count, send_count, mesh_layer,
                     MAC2STR(mesh_parent_addr.addr), MAC2STR(from.addr),
                     data.size, esp_get_free_heap_size(), flag, err, data.proto,
                     data.tos);
        }*/
    }
    vTaskDelete(NULL);
}

esp_err_t esp_mesh_comm_p2p_start(void)
{
    static bool is_comm_p2p_started = false;
    if (!is_comm_p2p_started) {
        is_comm_p2p_started = true;
        //xTaskCreate(esp_mesh_p2p_tx_main, "MPTX", 3072, NULL, 5, NULL);
        xTaskCreate(esp_mesh_p2p_rx_main, "MPRX", 3072, NULL, 5, NULL);
    }
    return ESP_OK;
}

void mesh_event_handler(mesh_event_t event)
{
    mesh_addr_t id = {0,};
    static uint8_t last_layer = 0;
    ESP_LOGD(MESH_TAG, "esp_event_handler:%d", event.id);

    switch (event.id) {
    case MESH_EVENT_STARTED:
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        break;
    case MESH_EVENT_STOPPED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        break;
    case MESH_EVENT_CHILD_CONNECTED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 event.info.child_connected.aid,
                 MAC2STR(event.info.child_connected.mac));
        break;
    case MESH_EVENT_CHILD_DISCONNECTED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 event.info.child_disconnected.aid,
                 MAC2STR(event.info.child_disconnected.mac));
        break;
    case MESH_EVENT_ROUTING_TABLE_ADD:
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d",
                 event.info.routing_table.rt_size_change,
                 event.info.routing_table.rt_size_new);
        break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE:
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d",
                 event.info.routing_table.rt_size_change,
                 event.info.routing_table.rt_size_new);
        break;
    case MESH_EVENT_NO_PARENT_FOUND:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 event.info.no_parent.scan_times);
        /* TODO handler for the failure */
        break;
    case MESH_EVENT_PARENT_CONNECTED:
        esp_mesh_get_id(&id);
        mesh_layer = event.info.connected.self_layer;
        memcpy(&mesh_parent_addr.addr, event.info.connected.connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR"",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr));
        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
        is_mesh_connected = true;
        if (esp_mesh_is_root()) {
            tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
        }
        esp_mesh_comm_p2p_start();
        break;
    case MESH_EVENT_PARENT_DISCONNECTED:
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 event.info.disconnected.reason);
        is_mesh_connected = false;
        mesh_disconnected_indicator();
        mesh_layer = esp_mesh_get_layer();
        break;
    case MESH_EVENT_LAYER_CHANGE:
        mesh_layer = event.info.layer_change.new_layer;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
        break;
    case MESH_EVENT_ROOT_ADDRESS:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(event.info.root_addr.addr));

        uint8_t chipid[15];
        esp_efuse_mac_get_default(chipid);
    
        sprintf(mac_address_root_str, ""MACSTR"", MAC2STR(chipid) );

        break;
    case MESH_EVENT_ROOT_GOT_IP:
        /* root starts to connect to server */
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_GOT_IP>sta ip: " IPSTR ", mask: " IPSTR ", gw: " IPSTR,
                 IP2STR(&event.info.got_ip.ip_info.ip),
                 IP2STR(&event.info.got_ip.ip_info.netmask),
                 IP2STR(&event.info.got_ip.ip_info.gw));
        gotIP = true;

         xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

        break;
    case MESH_EVENT_ROOT_LOST_IP:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_LOST_IP>");

         xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);

        break;
    case MESH_EVENT_VOTE_STARTED:
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                 event.info.vote_started.attempts,
                 event.info.vote_started.reason,
                 MAC2STR(event.info.vote_started.rc_addr.addr));
        break;
    case MESH_EVENT_VOTE_STOPPED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    case MESH_EVENT_ROOT_SWITCH_REQ:
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                 event.info.switch_req.reason,
                 MAC2STR( event.info.switch_req.rc_addr.addr));
        break;
    case MESH_EVENT_ROOT_SWITCH_ACK:
        /* new root */
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
        break;
    case MESH_EVENT_TODS_STATE:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d",
                 event.info.toDS_state);
        break;
    case MESH_EVENT_ROOT_FIXED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 event.info.root_fixed.is_fixed ? "fixed" : "not fixed");
        break;
    case MESH_EVENT_ROOT_ASKED_YIELD:
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                 MAC2STR(event.info.root_conflict.addr),
                 event.info.root_conflict.rssi,
                 event.info.root_conflict.capacity);
        break;
    case MESH_EVENT_CHANNEL_SWITCH:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", event.info.channel_switch.channel);
        break;
    case MESH_EVENT_SCAN_DONE:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 event.info.scan_done.number);
        break;
    case MESH_EVENT_NETWORK_STATE:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 event.info.network_state.is_rootless);
        break;
    case MESH_EVENT_STOP_RECONNECTION:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
        break;
    case MESH_EVENT_FIND_NETWORK:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                 event.info.find_network.channel, MAC2STR(event.info.find_network.router_bssid));
        break;
    case MESH_EVENT_ROUTER_SWITCH:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                 event.info.router_switch.ssid, event.info.router_switch.channel, MAC2STR(event.info.router_switch.bssid));
        break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%d", event.id);
        break;
    }
}


/**
 * Função de callback do stack MQTT; 
 * Por meio deste callback é possível receber as notificações com os status
 * da conexão e dos tópicos assinados e publicados;
 */
static esp_err_t mqtt_event_handler( esp_mqtt_event_handle_t event )
{
    int msg_id;
    /**
     * Repare que a variável client, no qual armazena o Handle da conexão socket
     * do MQTT foi colocada como global; 
     */
    client = event->client;
    
    switch (event->event_id) 
    {

        /*
         * Este case foi adicionado a partir da versão 3.2 do SDK-IDF;
         * e é necessário ser adicionado devido as configurações de error do compilador;
         */
          case MQTT_EVENT_BEFORE_CONNECT: 

            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            
          break;

        /**
         * Evento chamado quando o ESP32 se conecta ao broker MQTT, ou seja, 
         * caso a conexão socket TCP, SSL/TSL e autenticação com o broker MQTT
         * tenha ocorrido com sucesso, este evento será chamado informando que 
         * o ESP32 está conectado ao broker;
         */
        case MQTT_EVENT_CONNECTED:

            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            /**
             * Se chegou aqui é porque o ESP32 está conectado ao Broker MQTT; 
             * A notificação é feita setando em nível 1 o bit CONNECTED_BIT da 
             * variável mqtt_event_group;
             */
              msg_id = esp_mqtt_client_subscribe(client, "temperatura", 0); 
            //xEventGroupSetBits( mqtt_event_group, CONNECTED_BIT );
            break;
        /**
         * Evento chamado quando o ESP32 for desconectado do broker MQTT;
         */
        case MQTT_EVENT_DISCONNECTED:

            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");   
            /**
             * Se chegou aqui é porque o ESP32 foi desconectado do broker MQTT;
             */
                //xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
          break;

        /**
         * O eventos seguintes são utilizados para notificar quando um tópico é
         * assinado pelo ESP32;
         */
        case MQTT_EVENT_SUBSCRIBED:
            break;
        
        /**
         * Quando a assinatura de um tópico é cancelada pelo ESP32, 
         * este evento será chamado;
         */
        case MQTT_EVENT_UNSUBSCRIBED:
            break;
        
        /**
         * Este evento será chamado quando um tópico for publicado pelo ESP32;
         */
        case MQTT_EVENT_PUBLISHED:
            
            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        
        /**
         * Este evento será chamado quando uma mensagem chegar em algum tópico 
         * assinado pelo ESP32;
         */
        case MQTT_EVENT_DATA:

            if( DEBUG )
            {
                ESP_LOGI(TAG, "MQTT_EVENT_DATA"); 

                /**
                 * Sabendo o nome do tópico que publicou a mensagem é possível
                 * saber a quem data pertence;
                 */
                ESP_LOGI(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
                ESP_LOGI(TAG, "DATA=%.*s\r\n", event->data_len, event->data);               
            }  

            char topic_name[50];
            sprintf(topic_name, "%.*s", event->topic_len, event->topic);
            
            char topic_data[1024] = {0}; 
            snprintf(topic_data, sizeof(topic_data)-1,"%.*s", event->data_len, event->data); 

            if( processJson( topic_data ) == 0)
            {  
    
            } else {
                if( DEBUG )
                    ESP_LOGI(TAG, "error in processJson.\r\n"); 
            }

            break;
        
        /**
         * Evento chamado quando ocorrer algum erro na troca de informação
         * entre o ESP32 e o Broker MQTT;
         */
        case MQTT_EVENT_ERROR:
            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

int processJson( char * mb_request )
{
    int error = 0; 
    int num_error = 0;
    char buffer[200];
    char mac_str[30];
    int route_table_size = 0;
    mesh_addr_t route_table_mac;
esp_err_t err;
    mesh_data_t data;
    data.data = (uint8_t*)buffer;
    data.size = sizeof(buffer);
    data.proto = MESH_PROTO_BIN;


    const cJSON *resolution = NULL;
    cJSON *json = cJSON_Parse( mb_request );

    if( json != NULL )
    {
        
        cJSON *mb_publish_in = cJSON_GetObjectItem( json, "mqtt_publish_in" ); 
        if( mb_publish_in != NULL )
        {
            if(cJSON_IsString(mb_publish_in) && (mb_publish_in->valuestring != NULL))
            {
                if( DEBUG )
                    ESP_LOGI( TAG, "Publish In: %s.\r\n", mb_publish_in->valuestring );                    
                    
                    esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

                    for (int i = 0; i < route_table_size; i++) 
                    {
                        sprintf(mac_str, MACSTR, MAC2STR(route_table[i].addr));
                        if( strcmp(mb_publish_in->valuestring, (char*)mac_str) == 0 )
                        {
                            strcpy( (char*)buffer, "hello" );  

                            err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
                            if (err) {
                                
                            } 

                            break;
                        }
                    }
                    

                    

            }            
        } else {
            if( DEBUG )
                ESP_LOGI( TAG, "error in cJSON mqtt_publish_in.\r\n" ); 
            error = -1;        
        }    

        /**
         * modbus mb_device_reset
         */
        cJSON *mb_device_reset = cJSON_GetObjectItem( json, "mb_reset" ); 
        if( mb_device_reset != NULL )
        {
            if( cJSON_IsNumber( mb_device_reset ) )
            {
                if( DEBUG )
                    ESP_LOGI( TAG, "OK mb_reset=%d.\r\n", (int)mb_device_reset->valuedouble );  

                esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

                
                
                strcpy( buffer, "{\"root\":\"" );
                strcat(  buffer, mac_address_root_str);
                strcat( buffer, "\",\"devices\":[" );
                for (int i = 0; i < route_table_size; i++) 
                {   
                    sprintf(mac_str, MACSTR, MAC2STR(route_table[i].addr));

                    if( strcmp( mac_address_root_str, mac_str) != 0 )
                    {
                        strcat(  buffer, /*"{\"address\":\"" */"\"");
                        
                        strncat(  buffer, mac_str, sizeof(buffer));
                        strcat(  buffer, "\"");
                        if( (i+1) < route_table_size )
                        {
                          strcat(  buffer, ",");  
                        } else {
                            strcat(  buffer, "]}");

                            if(esp_mqtt_client_publish(client, "senai", buffer, strlen(buffer), 0, 0) == -1)
                            {
                                if( DEBUG )
                                    ESP_LOGI( TAG, "%s", "Error durante o envio via mqtt.\r\n" ); 
                            } 

                        }
                    }
                    ESP_LOGE(MESH_TAG,MACSTR,MAC2STR(route_table[i].addr));
                }


            }            
        } else {
            if( DEBUG )
                ESP_LOGI( TAG, "error in cJSON mb_reset.\r\n" );
            error = -1;         
        }    
           
        cJSON_Delete( json );

    } else {
        if( DEBUG )
            ESP_LOGI( TAG, "error in cJSON mb_write.\r\n" ); 
        error = -1;      
    }  

    
    return error; 
}

/**
 * Configuração do stack MQTT; 
 */
static void mqtt_app_start( void )
{
    /**
     * Sem SSL/TLS
     */
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://m12.cloudmqtt.com:17751",
        .event_handle = mqtt_event_handler,
        .username = "zrrnztby",
        .password = "u5NvnQW19_GA",
    };

    /**
     * Carrega configuração do descritor e inicializa stack MQTT;
     */
    esp_mqtt_client_handle_t client = esp_mqtt_client_init( &mqtt_cfg );
    esp_mqtt_client_start(client);
}

/**
 * Início do Programa;
 */
void app_main( void )
{
    ESP_ERROR_CHECK(mesh_light_init());
    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    tcpip_adapter_init();
    /* for mesh
     * stop DHCP server on softAP interface by default
     * stop DHCP client on station interface by default
     * */
    ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
    ESP_ERROR_CHECK(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));
#if 0
    /* static ip settings */
    tcpip_adapter_ip_info_t sta_ip;
    sta_ip.ip.addr = ipaddr_addr("192.168.1.102");
    sta_ip.gw.addr = ipaddr_addr("192.168.1.1");
    sta_ip.netmask.addr = ipaddr_addr("255.255.255.0");
    tcpip_adapter_set_ip_info(WIFI_IF_STA, &sta_ip);
#endif
    /*  wifi initialization */
    ESP_ERROR_CHECK(esp_event_loop_init(NULL, NULL));
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
#ifdef MESH_FIX_ROOT
    ESP_ERROR_CHECK(esp_mesh_fix_root(1));
#endif
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* mesh event callback */
    cfg.event_cb = &mesh_event_handler;
    /* router */
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d, %s\n",  esp_get_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed");
 vTaskDelay(10 * 1000 / portTICK_RATE_MS);

    wifi_event_group = xEventGroupCreate();

    /**
     * Aguarda a conexão via WiFi do ESP32 ao roteador;
     */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

    mqtt_app_start();

}
