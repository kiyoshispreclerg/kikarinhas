/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "ws.h"

TEST(urls)
{
    char host[64], path[128];
    int port;
    CHECK(kk_ws_parse_url("wss://chat.example.com/app/abc?protocol=7&x=y", host, sizeof host,
                          &port, path, sizeof path));
    CHECK_STR_EQ(host, "chat.example.com");
    CHECK_INT_EQ(port, 443);
    CHECK_STR_EQ(path, "/app/abc?protocol=7&x=y");

    CHECK(kk_ws_parse_url("wss://example.com:8443", host, sizeof host, &port, path, sizeof path));
    CHECK_INT_EQ(port, 8443);
    CHECK_STR_EQ(path, "/");
    CHECK(kk_ws_parse_url("wss://example.com?id=1", host, sizeof host, &port, path, sizeof path));
    CHECK_STR_EQ(path, "/?id=1");

    static const char *const bad[] = {
        "ws://example.com/",        /* only TLS */
        "https://example.com/",
        "wss:///path",
        "wss://exa mple.com/",
        "wss://example.com:0/",
        "wss://example.com:99999/",
        "wss://example.com/a b",
        "wss://example.com/#frag",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK_MSG(!kk_ws_parse_url(bad[i], host, sizeof host, &port, path, sizeof path), "%s",
                  bad[i]);
    /* Too long for the buffers. */
    CHECK(!kk_ws_parse_url("wss://example.com/0123456789", host, sizeof host, &port, path, 8));
}

/* The example in RFC 6455, section 1.3. */
TEST(accept_key)
{
    char out[32];
    kk_ws_accept_for("dGhlIHNhbXBsZSBub25jZQ==", out, sizeof out);
    CHECK_STR_EQ(out, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(upgrade_answer)
{
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    CHECK(kk_ws_check_upgrade("HTTP/1.1 101 Switching Protocols\r\n"
                              "upgrade: WebSocket\r\n"
                              "Connection: Upgrade\r\n"
                              "sec-websocket-accept:   s3pPLMBiTxaQ9kYGzzhZRbK+xOo=  \r\n\r\n",
                              key) == NULL);
    const char *why = kk_ws_check_upgrade("HTTP/1.1 403 Forbidden\r\n\r\n", key);
    CHECK(why && strstr(why, "403"));
    CHECK(kk_ws_check_upgrade("HTTP/1.1 101 OK\r\nUpgrade: websocket\r\n"
                              "Sec-WebSocket-Accept: wrong=\r\n\r\n",
                              key) != NULL);
    CHECK(kk_ws_check_upgrade("HTTP/1.1 101 OK\r\n"
                              "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
                              key) != NULL);
    CHECK(kk_ws_check_upgrade("SSH-2.0-x\r\n\r\n", key) != NULL);
}

/* Built by us (masked), read back. */
static void roundtrip(size_t len, int *failed)
{
    unsigned char *payload = malloc(len + 1), *frame = malloc(len + 14);
    for (size_t i = 0; i < len; i++)
        payload[i] = (unsigned char)(i * 7 + 3);
    static const unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
    size_t n = kk_ws_build_frame(KK_WS_TEXT, payload, len, mask, frame);
    size_t header = len < 126 ? 6 : len <= 0xFFFF ? 8 : 14;
    *failed = n != len + header;
    kk_ws_frame f;
    /* Short of one byte: not yet. */
    *failed |= kk_ws_parse_frame(frame, n - 1, 1u << 20, &f) != 0;
    *failed |= kk_ws_parse_frame(frame, n, 1u << 20, &f) != (long)n;
    *failed |= f.opcode != KK_WS_TEXT || !f.fin || f.len != len ||
               memcmp(f.payload, payload, len) != 0;
    free(payload);
    free(frame);
}

TEST(frames_roundtrip)
{
    static const size_t sizes[] = {0, 1, 125, 126, 1000, 65535, 65536, 200000};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        int failed;
        roundtrip(sizes[i], &failed);
        CHECK_MSG(!failed, "%zu bytes", sizes[i]);
    }
}

TEST(server_frames)
{
    /* Unmasked, as servers send them. */
    unsigned char text[] = {0x81, 0x05, 'h', 'e', 'l', 'l', 'o', 0x89, 0x00};
    kk_ws_frame f;
    CHECK_INT_EQ(kk_ws_parse_frame(text, sizeof text, 100, &f), 7);
    CHECK(f.fin && f.opcode == KK_WS_TEXT && f.len == 5 && memcmp(f.payload, "hello", 5) == 0);
    CHECK_INT_EQ(kk_ws_parse_frame(text + 7, 2, 100, &f), 2);
    CHECK(f.opcode == KK_WS_PING && f.len == 0);

    /* First fragment of a message. */
    unsigned char frag[] = {0x01, 0x02, 'h', 'e'};
    CHECK_INT_EQ(kk_ws_parse_frame(frag, sizeof frag, 100, &f), 4);
    CHECK(!f.fin && f.opcode == KK_WS_TEXT);

    /* 16-bit length, header only so far. */
    unsigned char ext[] = {0x81, 0x7E, 0x01};
    CHECK_INT_EQ(kk_ws_parse_frame(ext, sizeof ext, 1000, &f), 0);

    /* Malformed: reserved bits, a long control frame, a fragmented
     * control frame, over the limit. */
    unsigned char rsv[] = {0xC1, 0x00};
    CHECK_INT_EQ(kk_ws_parse_frame(rsv, sizeof rsv, 100, &f), -1);
    unsigned char ping[] = {0x89, 0x7E, 0x00, 0x80};
    CHECK_INT_EQ(kk_ws_parse_frame(ping, sizeof ping, 1000, &f), -1);
    unsigned char ping2[] = {0x09, 0x00};
    CHECK_INT_EQ(kk_ws_parse_frame(ping2, sizeof ping2, 1000, &f), -1);
    unsigned char big[] = {0x81, 0x7F, 0, 0, 0, 0, 0x10, 0, 0, 0};
    CHECK_INT_EQ(kk_ws_parse_frame(big, sizeof big, 1u << 20, &f), -1);
}

int main(void)
{
    RUN(urls);
    RUN(accept_key);
    RUN(upgrade_answer);
    RUN(frames_roundtrip);
    RUN(server_frames);
    return harness_report();
}
