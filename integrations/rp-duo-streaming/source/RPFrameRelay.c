#include <IOSurface/IOSurfaceAPI.h>
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xpc/xpc.h>

#define RP_FRAME_RELAY_SERVICE "com.rpduo.frame-relay"

// Both the listener and its peer connections deliver on the same serial queue.
struct relay_state {
    xpc_object_t surface;
    uint64_t sequence;
    uint64_t timestamp_ns;
};

static void clear_frame(struct relay_state *state)
{
    if (state->surface) {
        xpc_release(state->surface);
        state->surface = NULL;
    }
    state->timestamp_ns = 0;
}

static bool has_uint64(xpc_object_t message, const char *key)
{
    xpc_object_t value = xpc_dictionary_get_value(message, key);
    return value && xpc_get_type(value) == XPC_TYPE_UINT64;
}

static bool accept_frame(struct relay_state *state, xpc_object_t message)
{
    xpc_object_t surface = xpc_dictionary_get_value(message, "surface");
    uint64_t timestamp_ns = xpc_dictionary_get_uint64(message, "ts_ns");
    if (!surface || !has_uint64(message, "seq") || !timestamp_ns ||
        state->sequence == UINT64_MAX) {
        return false;
    }

    IOSurfaceRef image = IOSurfaceLookupFromXPCObject(surface);
    if (!image) {
        return false;
    }
    CFRelease(image);

    // The XPC object keeps the IOSurface in use even after the producer gets
    // its acknowledgement. Release stale frames as soon as they are replaced.
    clear_frame(state);
    state->surface = xpc_retain(surface);
    state->timestamp_ns = timestamp_ns;
    ++state->sequence;
    return true;
}

static void handle_request(struct relay_state *state, xpc_connection_t peer,
                           xpc_object_t message)
{
    if (xpc_get_type(message) != XPC_TYPE_DICTIONARY) {
        return;
    }
    if (xpc_connection_get_euid(peer) != geteuid()) {
        xpc_connection_cancel(peer);
        return;
    }

    const char *op = xpc_dictionary_get_string(message, "op");
    xpc_object_t reply = xpc_dictionary_create_reply(message);
    if (!reply) {
        return;
    }

    bool ok = false;
    if (op && strcmp(op, "frame") == 0) {
        ok = accept_frame(state, message);
        if (ok) {
            xpc_dictionary_set_uint64(reply, "seq", state->sequence);
        }
    } else if (op && strcmp(op, "next") == 0 && has_uint64(message, "after")) {
        uint64_t after = xpc_dictionary_get_uint64(message, "after");
        if (state->surface && state->sequence > after) {
            xpc_dictionary_set_value(reply, "surface", state->surface);
            xpc_dictionary_set_uint64(reply, "seq", state->sequence);
            xpc_dictionary_set_uint64(reply, "ts_ns", state->timestamp_ns);
        }
        ok = true;
    } else if (op && strcmp(op, "reset") == 0) {
        clear_frame(state);
        ok = true;
    }

    xpc_dictionary_set_bool(reply, "ok", ok);
    xpc_connection_send_message(peer, reply);
    xpc_release(reply);
}

int main(int argc, char **argv)
{
    if (argc > 2 || (argc == 2 && !argv[1][0])) {
        fprintf(stderr, "usage: %s [mach-service-name]\n", argv[0]);
        return 2;
    }
    const char *service = argc == 2 ? argv[1] : RP_FRAME_RELAY_SERVICE;
    static struct relay_state state;
    dispatch_queue_t queue = dispatch_queue_create("rpduo.frame-relay", DISPATCH_QUEUE_SERIAL);
    xpc_connection_t listener = xpc_connection_create_mach_service(
        service, queue, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!listener) {
        fprintf(stderr, "Could not create Mach service %s\n", service);
        return 1;
    }

    xpc_connection_set_event_handler(listener, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_CONNECTION) {
            xpc_connection_t peer = event;
            xpc_connection_set_target_queue(peer, queue);
            xpc_connection_set_event_handler(peer, ^(xpc_object_t message) {
                handle_request(&state, peer, message);
            });
            xpc_connection_activate(peer);
        } else if (type == XPC_TYPE_ERROR && event == XPC_ERROR_CONNECTION_INVALID) {
            fprintf(stderr, "Mach service %s became invalid\n", service);
            _exit(1);
        }
    });
    xpc_connection_activate(listener);
    dispatch_main();
}
