/*
 * Copyright (C) 2016 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define __BTSTACK_FILE__ "a2dp_sink.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack.h"
#include "avdtp.h"
#include "avdtp_util.h"
#include "avdtp_sink.h"
#include "a2dp_sink.h"

static const char * default_a2dp_sink_service_name = "BTstack A2DP Sink Service";
static const char * default_a2dp_sink_service_provider_name = "BTstack A2DP Sink Service Provider";

static avdtp_context_t a2dp_sink_context;

static a2dp_state_t app_state = A2DP_IDLE;
static avdtp_stream_endpoint_context_t sc;
static uint16_t avdtp_cid = 0;
// static int next_remote_sep_index_to_query = 0;

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

void a2dp_sink_create_sdp_record(uint8_t * service,  uint32_t service_record_handle, uint16_t supported_features, const char * service_name, const char * service_provider_name){
    uint8_t* attribute;
    de_create_sequence(service);

    // 0x0000 "Service Record Handle"
    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_SERVICE_RECORD_HANDLE);
    de_add_number(service, DE_UINT, DE_SIZE_32, service_record_handle);

    // 0x0001 "Service Class ID List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_SERVICE_CLASS_ID_LIST);
    attribute = de_push_sequence(service);
    {
        de_add_number(attribute, DE_UUID, DE_SIZE_16, BLUETOOTH_SERVICE_CLASS_AUDIO_SINK); 
    }
    de_pop_sequence(service, attribute);

    // 0x0004 "Protocol Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_PROTOCOL_DESCRIPTOR_LIST);
    attribute = de_push_sequence(service);
    {
        uint8_t* l2cpProtocol = de_push_sequence(attribute);
        {
            de_add_number(l2cpProtocol,  DE_UUID, DE_SIZE_16, BLUETOOTH_PROTOCOL_L2CAP);
            de_add_number(l2cpProtocol,  DE_UINT, DE_SIZE_16, BLUETOOTH_PROTOCOL_AVDTP);  
        }
        de_pop_sequence(attribute, l2cpProtocol);
        
        uint8_t* avProtocol = de_push_sequence(attribute);
        {
            de_add_number(avProtocol,  DE_UUID, DE_SIZE_16, BLUETOOTH_PROTOCOL_AVDTP);  // avProtocol_service
            de_add_number(avProtocol,  DE_UINT, DE_SIZE_16,  0x0103);  // version
        }
        de_pop_sequence(attribute, avProtocol);
    }
    de_pop_sequence(service, attribute);

    // 0x0005 "Public Browse Group"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_BROWSE_GROUP_LIST); // public browse group
    attribute = de_push_sequence(service);
    {
        de_add_number(attribute,  DE_UUID, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_PUBLIC_BROWSE_ROOT);
    }
    de_pop_sequence(service, attribute);

    // 0x0009 "Bluetooth Profile Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_BLUETOOTH_PROFILE_DESCRIPTOR_LIST);
    attribute = de_push_sequence(service);
    {
        uint8_t *a2dProfile = de_push_sequence(attribute);
        {
            de_add_number(a2dProfile,  DE_UUID, DE_SIZE_16, BLUETOOTH_SERVICE_CLASS_ADVANCED_AUDIO_DISTRIBUTION); 
            de_add_number(a2dProfile,  DE_UINT, DE_SIZE_16, 0x0103); 
        }
        de_pop_sequence(attribute, a2dProfile);
    }
    de_pop_sequence(service, attribute);


    // 0x0100 "Service Name"
    de_add_number(service,  DE_UINT, DE_SIZE_16, 0x0100);
    if (service_name){
        de_add_data(service,  DE_STRING, strlen(service_name), (uint8_t *) service_name);
    } else {
        de_add_data(service,  DE_STRING, strlen(default_a2dp_sink_service_name), (uint8_t *) default_a2dp_sink_service_name);
    }

    // 0x0100 "Provider Name"
    de_add_number(service,  DE_UINT, DE_SIZE_16, 0x0102);
    if (service_provider_name){
        de_add_data(service,  DE_STRING, strlen(service_provider_name), (uint8_t *) service_provider_name);
    } else {
        de_add_data(service,  DE_STRING, strlen(default_a2dp_sink_service_provider_name), (uint8_t *) default_a2dp_sink_service_provider_name);
    }

    // 0x0311 "Supported Features"
    de_add_number(service, DE_UINT, DE_SIZE_16, 0x0311);
    de_add_number(service, DE_UINT, DE_SIZE_16, supported_features);
}

void a2dp_sink_register_packet_handler(btstack_packet_handler_t callback){
    // avdtp_sink_register_packet_handler(callback);
    // return;
    if (callback == NULL){
        log_error("a2dp_sink_register_packet_handler called with NULL callback");
        return;
    }
    avdtp_sink_register_packet_handler(&packet_handler);   
    a2dp_sink_context.a2dp_callback = callback;
}

void a2dp_sink_register_media_handler(void (*callback)(avdtp_stream_endpoint_t * stream_endpoint, uint8_t *packet, uint16_t size)){
    if (callback == NULL){
        log_error("a2dp_sink_register_media_handler called with NULL callback");
        return;
    }
    avdtp_sink_register_media_handler(callback);   
}

void a2dp_sink_init(void){
    avdtp_sink_init(&a2dp_sink_context);
    l2cap_register_service(&packet_handler, BLUETOOTH_PROTOCOL_AVDTP, 0xffff, LEVEL_0);
}

uint8_t a2dp_sink_create_stream_endpoint(avdtp_media_type_t media_type, avdtp_media_codec_type_t media_codec_type, 
    uint8_t * codec_capabilities, uint16_t codec_capabilities_len,
    uint8_t * media_codec_info, uint16_t media_codec_info_len){
    avdtp_stream_endpoint_t * local_stream_endpoint = avdtp_sink_create_stream_endpoint(AVDTP_SINK, media_type);
    avdtp_sink_register_media_transport_category(avdtp_stream_endpoint_seid(local_stream_endpoint));
    avdtp_sink_register_media_codec_category(avdtp_stream_endpoint_seid(local_stream_endpoint), media_type, media_codec_type, 
        codec_capabilities, codec_capabilities_len);
    local_stream_endpoint->remote_configuration.media_codec.media_codec_information     = media_codec_info;
    local_stream_endpoint->remote_configuration.media_codec.media_codec_information_len = media_codec_info_len;
                           
    return local_stream_endpoint->sep.seid;
}

void a2dp_sink_establish_stream(bd_addr_t bd_addr, uint8_t local_seid){
    sc.local_stream_endpoint = avdtp_stream_endpoint_for_seid(local_seid, &a2dp_sink_context);
    if (!sc.local_stream_endpoint){
        log_error(" no local_stream_endpoint for seid %d", local_seid);
        return;
    }
    avdtp_sink_connect(bd_addr);
}

void a2dp_sink_disconnect(uint16_t a2dp_cid){
    avdtp_disconnect(a2dp_cid, &a2dp_sink_context);
}

static void a2dp_streaming_emit_connection_established(btstack_packet_handler_t callback, uint8_t * event, uint16_t event_size){
    if (!callback) return;
    if (event_size < 8) return;
    event[0] = HCI_EVENT_A2DP_META;
    event[2] = A2DP_SUBEVENT_STREAM_ESTABLISHED;
    (*callback)(HCI_EVENT_PACKET, 0, event, event_size);
}

static inline void a2dp_signaling_emit_media_codec_sbc(btstack_packet_handler_t callback, uint8_t * event, uint16_t event_size){
    if (!callback) return;
    if (event_size < 18) return;
    event[0] = HCI_EVENT_A2DP_META;
    event[2] = A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION;
    (*callback)(HCI_EVENT_PACKET, 0, event, event_size);
}

static inline void avdtp_signaling_emit_media_codec_other(btstack_packet_handler_t callback, uint8_t * event, uint16_t event_size){
    if (!callback) return;
    if (event_size < 112) return;
    event[0] = HCI_EVENT_A2DP_META;
    event[2] = A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION;
}

static inline void a2dp_emit_stream_event(btstack_packet_handler_t callback, uint16_t a2dp_cid, uint8_t eventID, uint8_t local_seid, uint8_t status){
    uint8_t event[7];
    int pos = 0;
    event[pos++] = HCI_EVENT_A2DP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = eventID;
    little_endian_store_16(event, pos, a2dp_cid);
    pos += 2;
    event[pos++] = local_seid;
    event[pos++] = status;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
    
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    bd_addr_t event_addr;
    uint8_t status;
    uint8_t  signal_identifier;
    uint16_t cid;
    uint8_t local_seid;

    switch (packet_type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case HCI_EVENT_PIN_CODE_REQUEST:
                    // inform about pin code request
                    printf("Pin code request - using '0000'\n");
                    hci_event_pin_code_request_get_bd_addr(packet, event_addr);
                    hci_send_cmd(&hci_pin_code_request_reply, &event_addr, 4, "0000");
                    break;
                case HCI_EVENT_DISCONNECTION_COMPLETE:
                    // connection closed -> quit test app
                    app_state = A2DP_IDLE;
                    printf("\n --- a2dp sink: HCI_EVENT_DISCONNECTION_COMPLETE ---\n");
                    break;
                case HCI_EVENT_AVDTP_META:
                    switch (packet[2]){
                        case AVDTP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
                            status = avdtp_subevent_signaling_connection_established_get_status(packet);
                            if (status != 0){
                                log_info(" --- a2dp sink --- AVDTP_SUBEVENT_SIGNALING_CONNECTION could not be established, status %d ---", status);
                                break;
                            }
                            app_state = A2DP_CONNECTED;
                            avdtp_cid = avdtp_subevent_signaling_connection_established_get_avdtp_cid(packet);
                            log_info(" --- a2dp sink --- AVDTP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED, avdtp cid 0x%02x ---", avdtp_cid);
                            break;

                        case AVDTP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION:
                            log_info(" --- a2dp sink ---  received non SBC codec. not implemented");
                            avdtp_signaling_emit_media_codec_other(a2dp_sink_context.a2dp_callback, packet, size);
                            break;
                        
                        case AVDTP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
                            if (app_state < A2DP_CONNECTED) return;
                            a2dp_signaling_emit_media_codec_sbc(a2dp_sink_context.a2dp_callback, packet, size);
                            break;

                        case AVDTP_SUBEVENT_STREAMING_CONNECTION_ESTABLISHED:
                            status = avdtp_subevent_streaming_connection_established_get_status(packet);
                            if (status != 0){
                                app_state = A2DP_CONNECTED;
                            } else {
                                app_state = A2DP_STREAMING_OPENED;
                            }
                            a2dp_streaming_emit_connection_established(a2dp_sink_context.a2dp_callback, packet, size);
                            break;

                        case AVDTP_SUBEVENT_SIGNALING_ACCEPT:
                            if (!a2dp_sink_context.a2dp_callback) return;
                            status = 0;
                            signal_identifier = avdtp_subevent_signaling_accept_get_signal_identifier(packet);
                            cid = avdtp_subevent_signaling_accept_get_avdtp_cid(packet);
                            local_seid = avdtp_subevent_signaling_accept_get_local_seid(packet);
                            printf(" --- a2dp sink ---  Accepted %d, a2dp sink cid 0x%2x, local seid %d\n", signal_identifier, cid, local_seid);
                            
                            switch (signal_identifier){
                                case  AVDTP_SI_START:
                                    a2dp_emit_stream_event(a2dp_sink_context.a2dp_callback, cid, A2DP_SUBEVENT_STREAM_STARTED, local_seid, status);
                                    break;
                                case AVDTP_SI_SUSPEND:
                                    a2dp_emit_stream_event(a2dp_sink_context.a2dp_callback, cid, A2DP_SUBEVENT_STREAM_SUSPENDED, local_seid, status);
                                    break;
                                case AVDTP_SI_ABORT:
                                case AVDTP_SI_CLOSE:
                                    a2dp_emit_stream_event(a2dp_sink_context.a2dp_callback, cid, A2DP_SUBEVENT_STREAM_RELEASED, local_seid, status);
                                    break;
                                default:
                                    break;
                            }
                            break;
                        
                        case AVDTP_SUBEVENT_SIGNALING_REJECT:
                        case AVDTP_SUBEVENT_SIGNALING_GENERAL_REJECT:
                            if (!a2dp_sink_context.a2dp_callback) return;
                            status = 1;
                            signal_identifier = avdtp_subevent_signaling_accept_get_signal_identifier(packet);
                            cid = avdtp_subevent_signaling_accept_get_avdtp_cid(packet);
                            local_seid = avdtp_subevent_signaling_accept_get_local_seid(packet);
                            printf(" --- a2dp sink ---  Rejected %d, a2dp sink cid 0x%2x, local seid %d\n", signal_identifier, cid, local_seid);
                            
                            switch (signal_identifier){
                                case  AVDTP_SI_START:
                                    a2dp_emit_stream_event(a2dp_sink_context.a2dp_callback, cid, A2DP_SUBEVENT_STREAM_STARTED, local_seid, status);
                                    break;
                                case AVDTP_SI_SUSPEND:
                                    a2dp_emit_stream_event(a2dp_sink_context.a2dp_callback, cid, A2DP_SUBEVENT_STREAM_SUSPENDED, local_seid, status);
                                    break;
                                case AVDTP_SI_ABORT:
                                case AVDTP_SI_CLOSE:
                                    a2dp_emit_stream_event(a2dp_sink_context.a2dp_callback, cid, A2DP_SUBEVENT_STREAM_RELEASED, local_seid, status);
                                    break;
                                default:
                                    break;
                            }
                            break;
                        default:
                            app_state = A2DP_IDLE;
                            log_info(" --- a2dp sink ---  not implemented");
                            break; 
                    }
                    break;   
                default:
                    break;
            }
            break;
        default:
            // other packet type
            break;
    }
}

