#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "wish_io.h"
#include "wish_debug.h"
#include "wish_connection_mgr.h"

#include "wish_relay_client.h"
#include "wish_time.h"

#include "utlist.h"

static void wish_core_relay_periodic(wish_core_t* core, void* ctx) {
    //WISHDEBUG(LOG_CRITICAL, "wish_core_relay_periodic");
    
    /* FIXME implementation for several relay connections */
    wish_relay_client_ctx_t *rctx = wish_relay_get_contexts(core);
    
    // return if no relay client found
    if (rctx == NULL) { return; }
    
    if (rctx->curr_state == WISH_RELAY_CLIENT_WAIT) {
        /* Just check timeouts if the relay client waits for
         * notifications from relay server */
        wish_relay_client_periodic(core, rctx);
    }
}

void wish_core_relay_client_init(wish_core_t* core) {
    int size = sizeof(wish_relay_client_ctx_t);
    wish_relay_client_ctx_t* client = wish_platform_malloc(size);
    memset(client, 0, size);

    client->ip.addr[0] = RELAY_SERVER_IP0;
    client->ip.addr[1] = RELAY_SERVER_IP1;
    client->ip.addr[2] = RELAY_SERVER_IP2;
    client->ip.addr[3] = RELAY_SERVER_IP3;
    client->port = RELAY_SERVER_PORT;
    
    LL_APPEND(core->relay_db, client);
    
    wish_core_time_set_interval(core, wish_core_relay_periodic, NULL, 1);
}

/* This function should be invoked regularly to process data received
 * from relay server and take actions accordingly */
void wish_relay_client_periodic(wish_core_t* core, wish_relay_client_ctx_t *relay) {
    switch (relay->curr_state) {
    case WISH_RELAY_CLIENT_OPEN:
        /* Establishing a Relay control connection:
         * After opening the TCP socket, the relay client must
         * send a Wish preable specifying connection type 6, and the UID
         * we request relaying for, so 3+32 bytes. */
        {
            const size_t handshake_len = 3+32;  /* preamble + uid */
            uint8_t handshake_data[handshake_len];
            handshake_data[0] = 'W';
            handshake_data[1] = '.';
            handshake_data[2] = (WISH_WIRE_VERSION << 4) | 
                WISH_WIRE_TYPE_RELAY_CONTROL;   /* Type: 6 */
            memcpy(handshake_data + 3, relay->relayed_uid, WISH_ID_LEN);
            relay->send(relay->send_arg, handshake_data, handshake_len);
            /* Advance state */
            relay->curr_state = WISH_RELAY_CLIENT_READ_SESSION_ID;
        }
        break;
    case WISH_RELAY_CLIENT_READ_SESSION_ID:
        /* If there are 10 bytes to be read from server... */
        if (ring_buffer_length(&(relay->rx_ringbuf)) >= RELAY_SESSION_ID_LEN) {
            /* Read the relay session ID, 10 bytes, storing it in the
             * relay context */
            ring_buffer_read(&(relay->rx_ringbuf), relay->session_id, 
                RELAY_SESSION_ID_LEN);
            /* Advance state */
            relay->curr_state = WISH_RELAY_CLIENT_WAIT;
            WISHDEBUG(LOG_CRITICAL, "Relay provided by: %i.%i.%i.%i:%d", relay->ip.addr[0], relay->ip.addr[1], relay->ip.addr[2], relay->ip.addr[3], relay->port);
            /* This a convenient place to make a first connection check, because we know at this point that we have a working Internet connection */
            wish_connections_check(core); 
        }
        break;
    case WISH_RELAY_CLIENT_WAIT:
        /* In this state the relay client expects the server to send
         * regular "keep-alive" messages (a '.' character every 10 secs)
         */
        /* FIXME How do we handle connection close? */
        if (ring_buffer_length(&(relay->rx_ringbuf)) >= 1) {
            uint8_t byte = 0;
            ring_buffer_read(&(relay->rx_ringbuf), &byte, 1);
            switch (byte) {
            case '.':
                /* Keepalive received - just ignore it */
                WISHDEBUG(LOG_DEBUG, "Relay: received keep-alive");
                break;
            case ':':
                /* We have a connection attempt to the relayed uid -
                 * Start accepting it! */
                WISHDEBUG(LOG_CRITICAL, "Relay: connection attempt!");

                /* Action plan: Open new Wish connection
                 * then send the session ID 
                 * Then proceed as if we were accepting a normal
                 * incoming Wish connection (in "server role", so to speak)
                 */

                /* Initialise connection with null IDs. 
                 * The actual IDs will be established during handshake
                 * */
                uint8_t null_id[WISH_ID_LEN] = { 0 };
                wish_connection_t *new_ctx = wish_connection_init(core, null_id, null_id);
                /* Register the relay context to the newly created wish
                 * context, this is because we need to send over the
                 * relay session id */
                if (new_ctx == NULL) {
                    WISHDEBUG(LOG_CRITICAL, "Cannot accept new connections at this time. Please try again later!");
                    break;
                }
                new_ctx->rctx = relay;
                new_ctx->via_relay = true;

                /* FIXME Implement some kind of abstraction for IP
                 * addresses */
                wish_open_connection(core, new_ctx, &(relay->ip), relay->port, true);
                break;
            default:
                WISHDEBUG(LOG_CRITICAL, "Relay error: Unexepected data");
                break;
            }
        } else {
            /* There was no data to read right now. Check that we are
             * not in "timeout" */
            if (wish_time_get_relative(core) > (relay->last_input_timestamp + RELAY_SERVER_TIMEOUT)) {
                WISHDEBUG(LOG_CRITICAL, "Relay control connection time-out");
                wish_relay_client_close(core, relay);
            }
        }

        break;
    case WISH_RELAY_CLIENT_CLOSING:
        WISHDEBUG(LOG_CRITICAL, "Waiting for relay client control connection to close properly");
        break;
    case WISH_RELAY_CLIENT_WAIT_RECONNECT:
        
        break;
    case WISH_RELAY_CLIENT_INITIAL:
        WISHDEBUG(LOG_CRITICAL, "Illegal wish relay state");
        break;
    }

}

void wish_relay_client_feed(wish_core_t* core, wish_relay_client_ctx_t *relay, uint8_t *data, size_t data_len) {
    ring_buffer_write(&(relay->rx_ringbuf), data, data_len);
    relay->last_input_timestamp = wish_time_get_relative(core);
}


int wish_relay_get_preferred_server_url(char *url_str, int url_str_max_len) {
    wish_platform_sprintf(url_str, "wish://%d.%d.%d.%d:%d", 
        RELAY_SERVER_IP0, RELAY_SERVER_IP1, RELAY_SERVER_IP2,
        RELAY_SERVER_IP3, RELAY_SERVER_PORT);
    return 0;
}
