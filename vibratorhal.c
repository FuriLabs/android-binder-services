// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>

#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gbinder.h>

#define VIBRATOR_DEVICE "/dev/anbox-hwbinder"
#define VIBRATOR_HIDL_IFACE "android.hardware.vibrator@1.0::IVibrator"
#define VIBRATOR_HIDL_NAME "default"

#define FEEDBACKD_DBUS_NAME "org.sigxcpu.Feedback"
#define FEEDBACKD_DBUS_PATH "/org/sigxcpu.Feedback"
#define FEEDBACKD_DBUS_IFACE "org.sigxcpu.Feedback"

#define RET_OK 0
#define RET_ERROR 1

enum IVibratorStatus {
    STATUS_OK = 0,
    STATUS_UNKNOWN_ERROR = 1,
    STATUS_BAD_VALUE = 2,
    STATUS_UNSUPPORTED_OPERATION = 3
};

enum IVibratorFunctions1_0 {
    VIBRATOR_ON = 1,
    VIBRATOR_OFF = 2,
    VIBRATOR_SUPPORTS_AMPLITUDE = 3,
    VIBRATOR_SET_AMPLITUDE = 4,
    VIBRATOR_PERFORM = 5
};

typedef struct {
    GMainLoop *loop;
    GBinderServiceManager *sm;
    GBinderLocalObject *obj;
    GDBusConnection *dbus;
    int ret;
} VibratorHal;

static gboolean
vibrator_signal(gpointer user_data)
{
    VibratorHal* hal = (VibratorHal*)user_data;

    g_debug("Caught signal, shutting down...");
    g_main_loop_quit(hal->loop);
    return G_SOURCE_CONTINUE;
}

static void
trigger_feedback_async(GDBusConnection *connection,
                       guint32 duration_ms)
{
    g_debug("Triggering vibration feedback");

    GVariantBuilder hint_builder;
    g_variant_builder_init(&hint_builder, G_VARIANT_TYPE("a{sv}"));

    GVariant *parameters = g_variant_new("(ssa{sv}i)",
                                         "android",
                                         "button-released",
                                         &hint_builder,
                                         -1);

    g_dbus_connection_call(g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL),
                           "org.sigxcpu.Feedback",
                           "/org/sigxcpu/Feedback",
                           "org.sigxcpu.Feedback",
                           "TriggerFeedback",
                           parameters,
                           G_VARIANT_TYPE("(u)"),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL,
                           NULL,
                           NULL);

    g_variant_builder_clear(&hint_builder);
}

static GBinderLocalReply*
vibrator_reply(GBinderLocalObject* obj,
               GBinderRemoteRequest* req,
               guint code,
               guint flags,
               int* status,
               void* user_data)
{
    VibratorHal* hal = (VibratorHal*)user_data;
    GBinderReader reader;
    GBinderWriter writer;
    GBinderLocalReply *reply = NULL;
    const char* iface = gbinder_remote_request_interface(req);

    g_debug("Received binder request: interface=%s code=%u", iface, code);

    if (g_strcmp0(iface, VIBRATOR_HIDL_IFACE) != 0) {
        g_warning("Unknown interface requested: %s", iface);
        return reply;
    }

    switch (code) {
        case VIBRATOR_ON:
            g_debug ("vibrator on");
            guint32 timeout_ms;

            gbinder_remote_request_init_reader(req, &reader);

            reply = gbinder_local_object_new_reply(obj);

            gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
            *status = GBINDER_STATUS_OK;

            gbinder_local_reply_init_writer(reply, &writer);

            gbinder_reader_read_uint32(&reader, &timeout_ms);
            trigger_feedback_async(hal->dbus, timeout_ms);
            gbinder_writer_append_int32(&writer, STATUS_OK);
            break;
        case VIBRATOR_OFF:
            g_debug ("vibrator off");
            reply = gbinder_local_object_new_reply(obj);
            gbinder_local_reply_init_writer(reply, &writer);
            gbinder_writer_append_int32(&writer, STATUS_OK);
            break;
        case VIBRATOR_SUPPORTS_AMPLITUDE:
            g_debug ("vibrator supports amplitude");
            reply = gbinder_local_object_new_reply(obj);

            gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
            *status = GBINDER_STATUS_OK;

            gbinder_local_reply_init_writer(reply, &writer);
            gbinder_writer_append_bool(&writer, FALSE);
            break;
        case VIBRATOR_SET_AMPLITUDE:
            g_debug ("vibrator set amplitude");
            reply = gbinder_local_object_new_reply(obj);

            gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
            *status = GBINDER_STATUS_OK;

            gbinder_local_reply_init_writer(reply, &writer);
            gbinder_writer_append_int32(&writer, STATUS_UNSUPPORTED_OPERATION);
            break;
        case VIBRATOR_PERFORM:
            g_debug ("vibrator perform");
            reply = gbinder_local_object_new_reply(obj);

            gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
            *status = GBINDER_STATUS_OK;

            gbinder_local_reply_init_writer(reply, &writer);
            gbinder_writer_append_int32(&writer, STATUS_UNSUPPORTED_OPERATION);
            gbinder_writer_append_int32(&writer, 0);
            break;
        default:
            g_warning("Unknown vibrator request code: %d", code);
            break;
    }

    return reply;
}

static void
vibrator_add_service_done(GBinderServiceManager* sm,
                          int status,
                          void* user_data)
{
    VibratorHal* hal = (VibratorHal*)user_data;

    if (status == GBINDER_STATUS_OK) {
        g_message("Added service \"%s\"", VIBRATOR_HIDL_NAME);
        hal->ret = RET_OK;
    } else {
        g_warning("Failed to add \"%s\" (%d)", VIBRATOR_HIDL_NAME, status);
        g_main_loop_quit(hal->loop);
    }
}

static void
vibrator_sm_presence_handler(GBinderServiceManager* sm,
                             void* user_data)
{
    VibratorHal* hal = (VibratorHal*)user_data;

    if (gbinder_servicemanager_is_present(hal->sm)) {
        g_debug("Service manager has reappeared");
        gbinder_servicemanager_add_service(hal->sm,
                                           VIBRATOR_HIDL_NAME,
                                           hal->obj,
                                           vibrator_add_service_done,
                                           hal);
    } else {
        g_debug("Service manager has died");
    }
}

static void
vibrator_run(VibratorHal* hal)
{
    guint sigterm = g_unix_signal_add(SIGTERM, vibrator_signal, hal);
    guint sigint = g_unix_signal_add(SIGINT, vibrator_signal, hal);
    gulong presence_id = gbinder_servicemanager_add_presence_handler(
        hal->sm, vibrator_sm_presence_handler, hal);

    hal->loop = g_main_loop_new(NULL, TRUE);

    gbinder_servicemanager_add_service(hal->sm,
                                       VIBRATOR_HIDL_NAME,
                                       hal->obj,
                                       vibrator_add_service_done,
                                       hal);

    g_message("Vibrator HAL service ready");

    g_main_loop_run(hal->loop);

    if (sigterm)
        g_source_remove(sigterm);
    if (sigint)
        g_source_remove(sigint);

    gbinder_servicemanager_remove_handler(hal->sm, presence_id);
    g_main_loop_unref(hal->loop);
    hal->loop = NULL;
}

int
main(void)
{
    VibratorHal hal;
    GError *error = NULL;

    memset(&hal, 0, sizeof(hal));
    hal.ret = RET_ERROR;

    hal.dbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!hal.dbus) {
        g_error("Failed to connect to system D-Bus: %s", error->message);
        g_error_free(error);
        return RET_ERROR;
    }

    hal.sm = gbinder_servicemanager_new2(VIBRATOR_DEVICE, "hidl", "hidl");
    if (gbinder_servicemanager_wait(hal.sm, -1)) {
        hal.obj = gbinder_servicemanager_new_local_object(
            hal.sm, VIBRATOR_HIDL_IFACE, vibrator_reply, &hal);

        vibrator_run(&hal);

        gbinder_local_object_unref(hal.obj);
        gbinder_servicemanager_unref(hal.sm);
    }

    if (hal.dbus)
        g_object_unref(hal.dbus);

    return hal.ret;
}
