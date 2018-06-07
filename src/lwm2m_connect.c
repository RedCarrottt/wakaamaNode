#include "lwm2m_connect.h"
#include "internal.h"
#include "wakaama/internals.h"
#include "client_debug.h"
#include "network.h"

lwm2m_context_t * contextP;

inline lwm2m_context_t * lwm2m_client_get_context() {
    return contextP;
}

void lwm2m_client_close(void) {
    lwm2m_close(contextP);
    lwm2m_network_close(contextP);

    lwm2m_object_t * securityObjP = contextP->objectList;
    lwm2m_object_t * serverObjP = securityObjP->next;

    lwm2m_list_free(securityObjP->instanceList);
    securityObjP->instanceList = NULL;
    lwm2m_list_free(serverObjP->instanceList);
    serverObjP->instanceList = NULL;

    contextP = NULL;
}

void lwm2m_device_res_has_changed(uint16_t res_id) {
    lwm2m_uri_t uri;
    uri.flag = 0;
    uri.objectId = 3; // device
    uri.instanceId = 0;
    uri.resourceId = res_id;
    lwm2m_resource_value_changed(contextP, &uri);
}

bool lwm2m_get_server_uri(uint16_t security_instance_id, char* uriBuffer, size_t buffer_len) {
    lwm2m_object_t * securityObjP = contextP->objectList;
    security_instance_t * securityInstance;
    securityInstance = (security_instance_t *)LWM2M_LIST_FIND(securityObjP->instanceList, security_instance_id);

    if (securityInstance == NULL)
        return false;

    strncpy(uriBuffer, securityInstance->uri, buffer_len);
    uriBuffer[buffer_len-1] = '\0';
    return true;
}

bool lwm2m_unregister_server(uint16_t security_instance_id)
{
    lwm2m_server_t * serverListEntry;
    serverListEntry = (lwm2m_server_t *)LWM2M_LIST_FIND(contextP->serverList, security_instance_id);
    if (serverListEntry != NULL)
    {
        registration_deregister(contextP, serverListEntry);
        serverListEntry->dirty = true;
        return true;
    }
    return false;
    //contextP->state = STATE_INITIAL;
}

void lwm2m_remove_unregistered_servers()
{
    lwm2m_object_t * securityObjP = contextP->objectList;
    lwm2m_object_t * serverObjP = securityObjP->next;

    lwm2m_server_t * serverListEntry = contextP->serverList;

    bool removedServers = false;
    while (serverListEntry != NULL)
    {
        if (serverListEntry->status != STATE_DEREGISTERED) {
            serverListEntry = serverListEntry->next;
            continue;
        }

        serverListEntry->dirty = true;

        uint16_t security_instance_id = serverListEntry->secObjInstID;

        security_instance_t * securityInstance;
        securityObjP->instanceList = LWM2M_LIST_RM(securityObjP->instanceList, security_instance_id, &securityInstance);
        if (securityInstance != NULL)
        {
            lwm2m_free(securityInstance);
        }

        server_instance_t * serverInstance;
        serverObjP->instanceList = LWM2M_LIST_RM(serverObjP->instanceList, security_instance_id, &serverInstance);
        if (serverInstance != NULL)
        {
            lwm2m_free(serverInstance);
        }

        removedServers = true;
        serverListEntry = serverListEntry->next;
    }

    if (removedServers)
        contextP->state = STATE_INITIAL;
}

bool lwm2m_add_server(uint16_t shortServerID, const char* uri, uint32_t lifetime, bool storing)
{
    lwm2m_object_t * securityObjP = contextP->objectList;
    lwm2m_object_t * serverObjP = securityObjP->next;

    security_instance_t * securityInstance = (security_instance_t *)lwm2m_malloc(sizeof(security_instance_t));
    server_instance_t * serverInstance = (server_instance_t *)lwm2m_malloc(sizeof(server_instance_t));

    if (NULL == securityInstance || serverInstance == NULL)
    {
        lwm2m_free(securityInstance);
        lwm2m_free(serverInstance);
        return false;
    }

    memset(securityInstance, 0, sizeof(security_instance_t));
    securityInstance->instanceId = lwm2m_list_newId(securityObjP->instanceList);
    strncpy(securityInstance->uri, uri, sizeof(securityInstance->uri));
    securityInstance->isBootstrap = false;
    securityInstance->shortID = shortServerID;
    securityInstance->clientHoldOffTime = 10;
    securityInstance->securityMode = LWM2M_SECURITY_MODE_NONE;

    securityObjP->instanceList = LWM2M_LIST_ADD(securityObjP->instanceList, securityInstance);

    memset(serverInstance, 0, sizeof(server_instance_t));
    serverInstance->instanceId = lwm2m_list_newId(serverObjP->instanceList);
    serverInstance->shortServerId = shortServerID;
    serverInstance->lifetime = lifetime;
    serverInstance->storing = storing;
    serverInstance->binding.data[0] = 'U';
    serverInstance->binding.data[1] = 0;
    serverObjP->instanceList = LWM2M_LIST_ADD(serverObjP->instanceList, serverInstance);

    contextP->state = STATE_INITIAL;
    return true;
}

void lwm2m_server_security_preshared(uint16_t shortServerID, const char* publicId, const char* psk, size_t pskLen) {
    lwm2m_object_t * securityObjP = contextP->objectList;
    security_instance_t * securityInstance;
    securityInstance = (security_instance_t *)LWM2M_LIST_FIND(securityObjP->instanceList, shortServerID);
    if (securityInstance == NULL)
        return;

    #ifdef LWM2M_WITH_DTLS
    if (publicId == NULL || psk == NULL)
    securityInstance->securityMode = LWM2M_SECURITY_MODE_NONE;
    else
        securityInstance->securityMode = LWM2M_SECURITY_MODE_PRE_SHARED_KEY;
    if (securityInstance->publicIdentity) lwm2m_free(securityInstance->publicIdentity);
    securityInstance->publicIdentity = NULL;
    securityInstance->publicIdentity = strdup(publicId);
    securityInstance->publicIdLen = strlen(publicId);
    if (securityInstance->secretKey) lwm2m_free(securityInstance->secretKey);
    if (psk) {
       securityInstance->secretKey = lwm2m_malloc(psk, pskLen);
       memcpy(securityInstance->secretKey, psk, pskLen);
    } else
       securityInstance->secretKey = NULL;
    securityInstance->secretKeyLen = pskLen;
    #endif
}
bool lwm2m_is_connected() {
    return contextP->state == STATE_READY;
}

uint8_t lwm2m_client_process(time_t* timeoutInSec) {
    return lwm2m_step(contextP, timeoutInSec);
}

lwm2m_context_t * lwm2m_client_init(const char * endpointName)
{
    lwm2m_object_t * objArray[3];

    contextP = lwm2m_init(NULL);
    if (!contextP)
    {
        return NULL;
    }

    objArray[0] = init_security_object();
    objArray[1] = init_server_object(contextP);
    objArray[2] = init_device_object();

    /*
     * We configure the liblwm2m library with the name of the client - which shall be unique for each client -
     * the number of objects we will be passing through and the objects array
     */
    int result = lwm2m_configure(contextP, endpointName, NULL, NULL, 3, objArray);
    if (result != 0)
    {
        lwm2m_printf("lwm2m_configure() failed: 0x%X\r\n", result);
        return NULL;
    }
    return contextP;
}
