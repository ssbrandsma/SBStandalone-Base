#ifndef SBUPNP_RPC_H
#define SBUPNP_RPC_H
#include "core.h"
#include "upnp_ipc.h"
/* Worker-only bounded HTTP SOAP. Every outer XML document passes the same
 * declaration/depth/size validation as embedded DIDL before DOM construction. */
unsigned sbupnp_rpc(const sbupnp_server *,const sb_upnp_request *,char *,size_t);
#endif
