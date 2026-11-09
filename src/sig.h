// Byte signatures for the two D2Net functions we hook. D2Net's code is identical across
// builds of 1.13, but the export ORDINALS differ between them, so the functions are found by their code.
#ifndef QM_SIG_H
#define QM_SIG_H
#include <string.h>

static unsigned sig_u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

// SendPacket(size, arg, packet), stdcall ret 0xc:   mov eax,[abs]; test eax,eax; jne +5; xor eax,eax; ret 0xc
// The tail (bytes 5..13) is the same whether or not another mod has already put a jmp over the first 5 bytes.
static int sig_is_send_tail(const unsigned char *p) {
    static const unsigned char tail[] = {0x85, 0xC0, 0x75, 0x05, 0x33, 0xC0, 0xC2, 0x0C, 0x00};
    return !memcmp(p + 5, tail, sizeof tail);
}
static int sig_is_send(const unsigned char *p) { return p[0] == 0xA1 && sig_is_send_tail(p); }
static int sig_is_send_hooked(const unsigned char *p) { return p[0] == 0xE9 && sig_is_send_tail(p); }

// ReceivePacket(buf, cap), stdcall ret 8 (server->client queue):
//   mov eax,[esp+4]; push eax; mov eax,[esp+0xc]; push <queue>; push <queue+4>; call ...; ret 8
static int sig_is_recv(const unsigned char *p) {
    static const unsigned char head[] = {0x8B, 0x44, 0x24, 0x04, 0x50, 0x8B, 0x44, 0x24, 0x0C, 0x68};
    if (memcmp(p, head, sizeof head) || p[14] != 0x68 || p[19] != 0xE8 || p[24] != 0xC2 || p[25] != 0x08) return 0;
    return sig_u32(p + 10) + 4 == sig_u32(p + 15);
}
#endif
