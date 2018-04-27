#include <stdlib.h>
#include <stdio.h>
#include <pomelo.h>
#include <stdbool.h>

#include "test_common.h"

static pc_client_t *g_client = NULL;

static void *
setup(const MunitParameter params[], void *data)
{
    Unused(data); Unused(params);
    // NOTE: use calloc in order to avoid one of the issues with the api.
    // see `issues.md`.
    g_client = calloc(1, pc_client_size());
    assert_not_null(g_client);
    return NULL;
}

static void
teardown(void *data)
{
    Unused(data);
    free(g_client);
    g_client = NULL;
}

static int SUCCESS_RECONNECT_EV_ORDER[] = {
    PC_EV_CONNECTED,
    PC_EV_UNEXPECTED_DISCONNECT,
    PC_EV_RECONNECT_STARTED,
    PC_EV_CONNECTED,
};

static void
reconnect_success_event_cb(pc_client_t* client, int ev_type, void* ex_data, const char* arg1, const char* arg2)
{
    int *num_calls = ex_data;
    assert_int(SUCCESS_RECONNECT_EV_ORDER[*num_calls], ==, ev_type);
    (*num_calls)++;
}

MunitResult
test_success(const MunitParameter params[], void *data)
{
    Unused(params); Unused(data);

    static int ports[] = {MOCK_TCP_PORT, MOCK_TLS_PORT};
    static int transports[] = {PC_TR_NAME_UV_TCP, PC_TR_NAME_UV_TLS};

    assert_true(tr_uv_tls_set_ca_file("../../test/server/fixtures/ca.crt", NULL));

    for (int i = 0; i < ArrayCount(ports); i++) {
        pc_client_config_t config = PC_CLIENT_CONFIG_DEFAULT;
        config.transport_name = transports[i];

        assert_int(pc_client_init(g_client, NULL, &config), ==, PC_RC_OK);

        int num_calls = 0;
        int handler_id = pc_client_add_ev_handler(g_client, reconnect_success_event_cb, &num_calls, NULL);
        assert_int(handler_id, !=, PC_EV_INVALID_HANDLER_ID);

        assert_int(pc_client_connect(g_client, LOCALHOST, ports[i], NULL), ==, PC_RC_OK);
        SLEEP_SECONDS(10);

        assert_int(num_calls, ==, ArrayCount(SUCCESS_RECONNECT_EV_ORDER));
        assert_int(pc_client_disconnect(g_client), ==, PC_RC_OK);
        assert_int(pc_client_rm_ev_handler(g_client, handler_id), ==, PC_RC_OK);
        assert_int(pc_client_cleanup(g_client), ==, PC_RC_OK);
    }

    return MUNIT_OK;
}

static int RECONNECT_MAX_RETRY_EV_ORDER[] = {
    PC_EV_CONNECT_ERROR,
    PC_EV_RECONNECT_STARTED,
    PC_EV_CONNECT_ERROR,
    PC_EV_CONNECT_ERROR,
    PC_EV_CONNECT_ERROR,
    PC_EV_RECONNECT_FAILED,
};

static void
reconnect_event_cb(pc_client_t* client, int ev_type, void* ex_data, const char* arg1, const char* arg2)
{
    int *num_called = ex_data;
    assert_int(ev_type, ==, RECONNECT_MAX_RETRY_EV_ORDER[*num_called]);
    (*num_called)++;
}

MunitResult
test_max_retry(const MunitParameter params[], void *data)
{
    Unused(data); Unused(params);

    pc_client_config_t config = PC_CLIENT_CONFIG_DEFAULT;
    config.reconn_max_retry = 3;
    config.reconn_delay = 1;

    assert_int(pc_client_init(g_client, NULL, &config), ==, PC_RC_OK);

    int num_called = 0;
    int handler_id = pc_client_add_ev_handler(g_client, reconnect_event_cb, &num_called, NULL);
    assert_int(handler_id, !=, PC_EV_INVALID_HANDLER_ID);

    assert_int(pc_client_connect(g_client, "invalidhost", TCP_PORT, NULL), ==, PC_RC_OK);
    SLEEP_SECONDS(8);

    // There should be reconn_max_retry reconnections retries. This means the event_cb should be called
    // reconn_max_retry + 1 times with PC_EV_CONNECT_ERROR and one time with PC_EV_RECONNECT_FAILED.
    assert_int(num_called, ==, ArrayCount(RECONNECT_MAX_RETRY_EV_ORDER));
    assert_int(pc_client_rm_ev_handler(g_client, handler_id), ==, PC_RC_OK);
    assert_int(pc_client_cleanup(g_client), ==, PC_RC_OK);

    return MUNIT_OK;
}

static MunitTest tests[] = {
    {"/max_retry", test_max_retry, setup, teardown, MUNIT_TEST_OPTION_NONE, NULL},
    {"/success", test_success, setup, teardown, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

const MunitSuite reconnection_suite = {
    "/reconnection", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};