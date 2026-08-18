#include <Foundation/Foundation.h>
#include <bsm/libbsm.h>
#include <libproc.h>

#include <libjailbreak/libjailbreak.h>
#include <libjailbreak/roothider.h>

void jailbreakd_reply_message(JBD_MESSAGE_ID msgId, xpc_object_t reply)
{
	char* desc = NULL;
	JBLogDebug("reply message %d with %s", msgId, (desc=xpc_copy_description(reply)));
	if(desc) free(desc);
	int err = xpc_pipe_routine_reply(reply);
	if (err != 0) {
		JBLogError("Error %d sending response", err);
	}
}

void jailbreakd_received_message(mach_port_t port)
{
	@autoreleasepool {
		xpc_object_t message = nil;
		int err = xpc_pipe_receive(port, &message);
		if (err != 0) {
			JBLogError("xpc_pipe_receive error %d", err);
			return;
		}

		xpc_object_t reply = xpc_dictionary_create_reply(message);

		JBD_MESSAGE_ID msgId = xpc_dictionary_get_uint64(message, "id");
		
		if (xpc_get_type(message) == XPC_TYPE_DICTIONARY) {
			audit_token_t auditToken = {0};
			xpc_dictionary_get_audit_token(message, &auditToken);
			uid_t clientUid = audit_token_to_euid(auditToken);
			pid_t clientPid = audit_token_to_pid(auditToken);

			char* desc = NULL;
			JBLogDebug("received message %d from %d(%s) with dictionary: %s", msgId, clientPid, proc_get_path(clientPid,NULL), (desc=xpc_copy_description(message)));
			if(desc) free(desc);

			switch (msgId) {
				case JBD_MSG_SPINLOCK_FIX_ONLY: {
					int64_t result = 0;
					pid_t pid = xpc_dictionary_get_int64(message, "pid");
					bool resume = xpc_dictionary_get_bool(message, "resume");
					pid_t ppid = proc_get_ppid(pid);
					JBLogDebug("spinlock fix: client pid=%d, child pid=%d, child's parent pid=%d, child proc=%s", clientPid, pid, ppid, proc_get_path(pid,NULL));
					if(ppid == clientPid) {
						if(ppid==1 && resume==false) {
							// Same rationale as JBD_MSG_SPAWN_PATCH_CHILD below:
							// On userspace reboot, we MUST apply the full dyld patch to the
							// new launchd, otherwise DYLD_INSERT_LIBRARIES=launchdhook.dylib
							// is stripped by AMFI for the platform launchd binary and launchd
							// can't bootstrap (kernel panics with "initproc exited
							// exit reason namespace 2 subcode 0xa").
							bool iOS15 = false;
#ifdef __arm64e__
							iOS15 = !__builtin_available(iOS 16.0, *);
#endif
							if (iOS15) {
								result = proc_patch_csflags(pid);
							}
							else if (roothide_patch_proc(pid) == 0) {
								// Patched via dyld; do not SIGCONT (resume is false).
							} else {
								JBLogError("launchd spinlock-fix (dyld) failed: %d", pid);
								result = proc_patch_csflags(pid);
							}
						}
						else if(proc_fix_spinlock(pid) == 0) {
							if(resume) kill(pid, SIGCONT);
						} else {
							JBLogError("spinlock fix failed: %d", pid);
							result = -1;
						}
					} else {
						JBLogError("spinlock fix denied: %d", pid);
						result = -1;
					}
					xpc_dictionary_set_int64(reply, "result", result);
					break;
				}

				case JBD_MSG_SPAWN_PATCH_CHILD: {
					int64_t result = 0;
					pid_t pid = xpc_dictionary_get_int64(message, "pid");
					bool resume = xpc_dictionary_get_bool(message, "resume");
					pid_t ppid = proc_get_ppid(pid);
					JBLogDebug("spawn patch: client pid=%d, child pid=%d, child's parent pid=%d, child proc=%s", clientPid, pid, ppid, proc_get_path(pid,NULL));
					if(ppid == clientPid) {
						if(ppid==1 && resume==false) {
							// This branch is reached on userspace reboot: launchd (pid 1) is
							// posix_spawn'ing a new /sbin/launchd, suspended, and is asking us to
							// patch it before resume. Without a proper dyld patch here, the new
							// launchd will load the STOCK dyld, which strips DYLD_INSERT_LIBRARIES
							// for platform binaries, so launchdhook.dylib is never injected, the
							// bootstrap port is never set up, and launchd calls
							// exit_with_reason(OS_REASON_SYSTEM, 0xa) -> kernel panics with
							// "initproc exited -- exit reason namespace 2 subcode 0xa".
							//
							// Historically (the comment below), `frida -f` on iOS 15 had issues
							// with proc_patch_dyld, so the code path was forced to use
							// proc_patch_csflags (which only sets CS_GET_TASK_ALLOW). That worked
							// on iOS 15 because the dyld patch was applied through other means
							// (spinlock fix + fakelib bind mount). On iOS 16+ (especially 16.7.x
							// on A11/T8015), the dyld patch is the ONLY reliable way to make
							// DYLD_INSERT_LIBRARIES work for the new launchd.
							//
							// Fix: only use the csflags-only path on iOS 15. On iOS 16+, call
							// roothide_patch_proc(pid), which will call proc_patch_dyld(pid) when
							// dyld_patch_enabled() is true OR process_force_dyld_patch returns
							// true for /sbin/launchd (the latter is enforced in common.m).
							bool iOS15 = false;
#ifdef __arm64e__
							iOS15 = !__builtin_available(iOS 16.0, *);
#endif
							if (iOS15) {
								result = proc_patch_csflags(pid);
							}
							else if (roothide_patch_proc(pid) == 0) {
								// proc_patch_dyld succeeded; child is patched and will load
								// launchdhook.dylib via DYLD_INSERT_LIBRARIES when resumed.
								// resume is false here (we are at userspace reboot), so do NOT
								// send SIGCONT. The kernel will resume launchd when posix_spawn
								// returns to the OLD launchd, which will then exit itself.
							} else {
								JBLogError("launchd spawn patch (dyld) failed: %d", pid);
								// Last-resort fallback to the old csflags-only behavior so we
								// don't leave launchd suspended forever. This will likely still
								// panic, but at least the failure mode is observable.
								result = proc_patch_csflags(pid);
								if (result == 0) {
									JBLogError("falling back to csflags-only patch (may panic)");
								}
							}
						}
						else if(roothide_patch_proc(pid) == 0) {
							if(resume) kill(pid, SIGCONT);
						} else {
							JBLogError("spawn patch failed: %d", pid);
							result = -1;
						}
					} else {
						JBLogError("spawn patch denied: %d", pid);
						result = -1;
					}
					xpc_dictionary_set_int64(reply, "result", result);
					break;
				}

				case JBD_MSG_SPAWN_EXEC_START: {
					bool resume = xpc_dictionary_get_bool(message, "resume");
					const char* execfile = xpc_dictionary_get_string(message, "execfile");
					JBLogDebug("spawn exec start: %d %s", clientPid, execfile);
					int64_t result = spawnExecPatchAdd(clientPid, resume);
					xpc_dictionary_set_int64(reply, "result", result);
					break;
				}

				case JBD_MSG_SPAWN_EXEC_CANCEL: {
					const char* execfile = xpc_dictionary_get_string(message, "execfile");
					JBLogDebug("spawn exec cancel: %d %s", clientPid, execfile);
					int64_t result = spawnExecPatchDel(clientPid);
					xpc_dictionary_set_int64(reply, "result", result);
					break;
				}

				case JBD_MSG_EXEC_TRACE_START: {
					//dead lock: jbd->ptrace->kernel->amfi port->launchd->spawn amfid->jdb
					dispatch_async(dispatch_get_global_queue(0, 0), ^{
						int64_t result = -1;
						uint64_t traced = xpc_dictionary_get_uint64(message, "traced");
						const char* execfile = xpc_dictionary_get_string(message, "execfile");
						JBLogDebug("exec trace start: %d %s", clientPid, execfile);
						result = execTraceProcess(clientPid, traced);
						xpc_dictionary_set_int64(reply, "result", result);
						jailbreakd_reply_message(msgId, reply);
					});
					reply = nil; //reply later
					break;
				}

				case JBD_MSG_EXEC_TRACE_CANCEL: {
					int64_t result = -1;
					uint64_t detached = xpc_dictionary_get_uint64(message, "detached");
					const char* execfile = xpc_dictionary_get_string(message, "execfile");
					JBLogDebug("exec trace cancel: %d %s", clientPid, execfile);
					result = execTraceCancel(clientPid, detached);
					xpc_dictionary_set_int64(reply, "result", result);
					break;
				}

				case JBD_MSG_SYSTEMWIDE_LOG: {
#ifdef ENABLE_LOGS
					static char logFilePath[PATH_MAX] = {0};
					static dispatch_once_t onceToken;
					dispatch_once(&onceToken, ^{
						JBLogGetLogFilePath("systemwide", NULL, logFilePath);
					});

					const char* progname = NULL;
					const char* procpath = proc_get_path(clientPid,NULL);
					if(procpath) {
						progname = strrchr(procpath, '/');
						if(progname) progname++; else progname = procpath;
					}
					uint64_t tid = xpc_dictionary_get_uint64(message, "tid");
					const char* log = xpc_dictionary_get_string(message, "log");
					JBLogFunction(logFilePath, clientPid, tid, progname ? progname : "(null)", "%s", log);
					xpc_dictionary_set_int64(reply, "result", 0);
#else
					abort();
#endif
					break;
				}

				case JBD_MSG_TEST_CALL: {
					int value = xpc_dictionary_get_int64(message, "value");
					JBLogDebug("jailbreakd test call(%llu) from %d,%s", value, clientPid, proc_get_path(clientPid,NULL));	
					xpc_dictionary_set_int64(reply, "result", value * 2);
					
					if(clientUid == 0) {
						abort(); // crashreporter test
					}

					break;
				}
			}
		}
		if (reply) {
			jailbreakd_reply_message(msgId, reply);
		}
	}
}
