#ifndef JBSERVER_XPC_H
#define JBSERVER_XPC_H

#include <libjailbreak/jbserver.h>
#include <xpc/xpc.h>

int jbserver_received_xpc_message(struct jbserver_impl *server, xpc_object_t xmsg);

int trustcache_nokcall_ipc_append_entries(xpc_object_t entriesData);
int trustcache_nokcall_ipc_query_cdhash(xpc_object_t hashData, bool *foundOut);
int trustcache_nokcall_ipc_owner_status(void);

#endif
