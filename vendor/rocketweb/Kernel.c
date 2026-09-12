#include "Kernel.h"
#include "generated/reference.h"
#include "generated/arena_data.h"
#include "wasm-rt-impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct w2c_a
{
    w2c_rocketweb* engine;
};
struct NeonRocketKernel
{
    w2c_rocketweb  engine;
    struct w2c_a   host;
    char           error[80];
    unsigned char* baseline;
    size_t         baseline_size;
    uint32_t       baseline_hash;
};
static void* ptr(struct w2c_a* a, u32 p, size_t len)
{
    wasm_rt_memory_t* m = &a->engine->w2c_m;
    if ((uint64_t)p + len > m->size)
        wasm_rt_trap(WASM_RT_TRAP_OOB);
    return m->data + p;
}
u32 w2c_a_a(struct w2c_a* a, u32 p, u32 q)
{
    *(u32*)ptr(a, p, 4) = 0;
    *(u32*)ptr(a, q, 4) = 0;
    return 0;
}
u32 w2c_a_b(struct w2c_a* a, u32 p, u64 q, u32 r, u32 s)
{
    return 8;
}
u32 w2c_a_c(struct w2c_a* a, u32 p, u32 q, u32 r, u32 s)
{
    return 8;
}
u32 w2c_a_d(struct w2c_a* a, u32 p)
{
    return 8;
}
u32 w2c_a_e(struct w2c_a* a, u32 p, u64 q, u32 r)
{
    *(u64*)ptr(a, r, 8) = 0;
    return 0;
}
u32 w2c_a_f(struct w2c_a* a, u32 p)
{
    wasm_rt_memory_t* m = &a->engine->w2c_m;
    return wasm_rt_grow_memory(m, (p - m->size + 65535) / 65536) != 0xffffffff;
}
u32 w2c_a_g(struct w2c_a* a, u32 fd, u32 iov, u32 count, u32 out)
{
    u32 sum = 0;
    for (u32 i = 0; i < count; i++)
    {
        u32* v = ptr(a, iov + i * 8, 8);
        fwrite(ptr(a, v[0], v[1]), 1, v[1], stderr);
        sum += v[1];
    }
    *(u32*)ptr(a, out, 4) = sum;
    return 0;
}
void w2c_a_h(struct w2c_a* a, u32 p)
{
    fprintf(stderr, "exit %u\n", p);
    wasm_rt_trap(WASM_RT_TRAP_UNREACHABLE);
}
u32 w2c_a_i(struct w2c_a* a, u32 p, f64 q)
{
    fprintf(stderr, "unexpected timer\n");
    wasm_rt_trap(WASM_RT_TRAP_UNREACHABLE);
}
u32 w2c_a_j(struct w2c_a* a, u32 p, u32 q)
{
    return 0;
}
void w2c_a_k(struct w2c_a* a)
{
}
void w2c_a_l(struct w2c_a* a)
{
    wasm_rt_trap(WASM_RT_TRAP_UNREACHABLE);
}

static int trap_error(NeonRocketKernel* k, int trap)
{
    snprintf(k->error, sizeof(k->error), "Reference kernel trap: %d", trap);
    return 0;
}
#define GUARD(k) \
    int trap = wasm_rt_impl_try(); \
    if (trap) \
    return trap_error(k, trap)
NeonRocketKernel* nrk_create(void)
{
    wasm_rt_init();
    NeonRocketKernel* k = calloc(1, sizeof(*k));
    if (!k)
        return NULL;
    k->host.engine = &k->engine;
    int trap = wasm_rt_impl_try();
    if (trap)
    {
        wasm2c_rocketweb_free(&k->engine);
        free(k);
        return NULL;
    }
    wasm2c_rocketweb_instantiate(&k->engine, &k->host);
    w2c_rocketweb_n(&k->engine);
    u32 sizes = w2c_rocketweb_I(&k->engine, sizeof(arena_sizes));
    u32 data = w2c_rocketweb_I(&k->engine, sizeof(arena_data));
    memcpy(ptr(&k->host, sizes, sizeof(arena_sizes)), arena_sizes, sizeof(arena_sizes));
    memcpy(ptr(&k->host, data, sizeof(arena_data)), arena_data, sizeof(arena_data));
    u32 ok = w2c_rocketweb_o(&k->engine, data, sizes, 16);
    w2c_rocketweb_G(&k->engine, data);
    w2c_rocketweb_G(&k->engine, sizes);
    if (!ok)
    {
        wasm2c_rocketweb_free(&k->engine);
        free(k);
        return NULL;
    }
    return k;
}
void nrk_destroy(NeonRocketKernel* k)
{
    if (k)
    {
        wasm2c_rocketweb_free(&k->engine);
        free(k->baseline);
        free(k);
    }
}
const char* nrk_error(NeonRocketKernel* k)
{
    return k ? k->error : "Cannot allocate reference kernel";
}
int nrk_command(NeonRocketKernel* k, int op, int a, int b)
{
    GUARD(k);
    w2c_rocketweb* e = &k->engine;
    switch (op)
    {
        case 0:
            if (!w2c_rocketweb_p(e))
                return 0;
            if (w2c_rocketweb_q(e, b ? (a ? 0 : 1) : 0, a) != 0)
                return 0;
            if (b && w2c_rocketweb_q(e, a ? 1 : 0, 0) != 1)
                return 0;
            break;
        case 1:
            w2c_rocketweb_s(e, a);
            break;
        case 2:
            w2c_rocketweb_t(e, a);
            break;
        case 3:
            w2c_rocketweb_u(e);
            break;
        case 4:
            return w2c_rocketweb_v(e, a, b);
        case 5:
            w2c_rocketweb_w(e, a);
            break;
        case 6:
            w2c_rocketweb_F(e);
            break;
        default:
            return 0;
    }
    return 1;
}
int nrk_controls(NeonRocketKernel* k, int car, const float v[8])
{
    GUARD(k);
    if (car < 0 || car >= 8)
        return 0;
    memcpy(ptr(&k->host, w2c_rocketweb_z(&k->engine) + car * 32, 32), v, 32);
    return 1;
}
int nrk_state(NeonRocketKernel* k, float* out, size_t cap)
{
    GUARD(k);
    u32 n = w2c_rocketweb_y(&k->engine);
    if (n > cap)
        return 0;
    memcpy(out, ptr(&k->host, w2c_rocketweb_x(&k->engine), n * 4), n * 4);
    return n;
}
int nrk_pads(NeonRocketKernel* k, float* out, size_t cap)
{
    GUARD(k);
    float* s = ptr(&k->host, w2c_rocketweb_x(&k->engine), 16);
    u32    n = (u32)s[3] * 4;
    if (n > cap)
        return 0;
    memcpy(out, ptr(&k->host, w2c_rocketweb_A(&k->engine), n * 4), n * 4);
    return n;
}
int nrk_camera(NeonRocketKernel* k, const double* in, double* out)
{
    GUARD(k);
    u32 p = w2c_rocketweb_D(&k->engine);
    memcpy(ptr(&k->host, p, 28 * 8), in, 28 * 8);
    w2c_rocketweb_E(&k->engine);
    memcpy(out, ptr(&k->host, p, 42 * 8), 42 * 8);
    return 1;
}
int nrk_ball_grounded(NeonRocketKernel* k)
{
    GUARD(k);
    return (int)w2c_rocketweb_B(&k->engine);
}

const unsigned char* nrk_collision_data(size_t* length)
{
    *length = sizeof(arena_data);
    return arena_data;
}

/* The pinned module's only mutable global is its stack pointer. Its funcref
   table is initialized once and never mutated; retain each host's own table.
   Full-memory differences retain allocator, contact and jump state, unlike the
   public render snapshot. The baseline and codec are identical on x86/x64. */
static uint32_t checkpoint_hash(const unsigned char* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static void checkpoint_u32(unsigned char* p, uint32_t n)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (8 * i));
}
static uint32_t checkpoint_read(const unsigned char* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int nrk_checkpoint_prepare(NeonRocketKernel* k)
{
    if (!k || k->engine.w2c_m.size > 64u * 1024 * 1024)
        return 0;
    size_t         size = (size_t)k->engine.w2c_m.size;
    unsigned char* baseline = malloc(size);
    if (!baseline)
        return 0;
    memcpy(baseline, k->engine.w2c_m.data, size);
    free(k->baseline);
    k->baseline = baseline;
    k->baseline_size = size;
    k->baseline_hash = checkpoint_hash(baseline, size);
    return 1;
}
size_t nrk_checkpoint_save(NeonRocketKernel* k, unsigned char* out, size_t cap)
{
    if (!k || !k->baseline || k->baseline_size != k->engine.w2c_m.size || !out || cap < 24)
        return 0;
    size_t               cursor = 24;
    uint32_t             records = 0;
    const unsigned char* memory = k->engine.w2c_m.data;
    for (size_t offset = 0; offset < k->baseline_size; offset += 256)
    {
        if (!memcmp(memory + offset, k->baseline + offset, 256))
            continue;
        if (cap - cursor < 292)
            return 0;
        checkpoint_u32(out + cursor, (uint32_t)offset);
        unsigned char* mask = out + cursor + 4;
        memset(mask, 0, 32);
        cursor += 36;
        for (size_t i = 0; i < 256; ++i)
        {
            unsigned char delta = memory[offset + i] ^ k->baseline[offset + i];
            if (delta)
            {
                mask[i / 8] |= (unsigned char)(1u << (i % 8));
                out[cursor++] = delta;
            }
        }
        ++records;
    }
    memcpy(out, "NRP1", 4);
    checkpoint_u32(out + 4, (uint32_t)k->baseline_size);
    checkpoint_u32(out + 8, k->baseline_hash);
    checkpoint_u32(out + 12, k->engine.w2c_g0);
    checkpoint_u32(out + 16, records);
    checkpoint_u32(out + 20, checkpoint_hash(out + 24, cursor - 24));
    return cursor;
}
static int checkpoint_validate(NeonRocketKernel* k, const unsigned char* data, size_t length)
{
    if (!k || !k->baseline || !data || length < 24 || length > 512u * 1024 || memcmp(data, "NRP1", 4))
        return 0;
    if (checkpoint_read(data + 4) != k->baseline_size || k->engine.w2c_m.size != k->baseline_size || checkpoint_read(data + 8) != k->baseline_hash ||
        checkpoint_read(data + 12) > k->baseline_size || checkpoint_read(data + 20) != checkpoint_hash(data + 24, length - 24))
        return 0;
    uint32_t count = checkpoint_read(data + 16), previous = 0;
    size_t   cursor = 24;
    if (count > k->baseline_size / 256)
        return 0;
    // Validate the entire input before changing a live simulation.
    for (uint32_t record = 0; record < count; ++record)
    {
        if (length - cursor < 36)
            return 0;
        uint32_t offset = checkpoint_read(data + cursor);
        if (offset % 256 || offset > k->baseline_size - 256 || (record && offset <= previous))
            return 0;
        previous = offset;
        const unsigned char* mask = data + cursor + 4;
        cursor += 36;
        for (int i = 0; i < 32; ++i)
            for (int bit = 0; bit < 8; ++bit)
                if (mask[i] & (1u << bit))
                {
                    if (cursor >= length)
                        return 0;
                    ++cursor;
                }
    }
    if (cursor != length)
        return 0;
    return 1;
}
static void checkpoint_apply(unsigned char* memory, const unsigned char* data)
{
    uint32_t count = checkpoint_read(data + 16);
    size_t   cursor = 24;
    for (uint32_t record = 0; record < count; ++record)
    {
        uint32_t             offset = checkpoint_read(data + cursor);
        const unsigned char* mask = data + cursor + 4;
        cursor += 36;
        for (int i = 0; i < 256; ++i)
            if (mask[i / 8] & (1u << (i % 8)))
                memory[offset + i] ^= data[cursor++];
    }
}
int nrk_checkpoint_restore(NeonRocketKernel* k, const unsigned char* data, size_t length)
{
    if (!checkpoint_validate(k, data, length))
        return 0;
    unsigned char* memory = k->engine.w2c_m.data;
    for (size_t offset = 0; offset < k->baseline_size; offset += 256)
        if (memcmp(memory + offset, k->baseline + offset, 256))
            memcpy(memory + offset, k->baseline + offset, 256);
    checkpoint_apply(memory, data);
    k->engine.w2c_g0 = checkpoint_read(data + 12);
    return 1;
}
int nrk_checkpoint_rebase(NeonRocketKernel* k, unsigned char* data, size_t length)
{
    if (!checkpoint_validate(k, data, length))
        return 0;
    // Move only the reference memory. The client may currently be predicting
    // ahead: receiving this reference must not rewind its live world or camera.
    // The validated packet incorporates the previous identity and all changes,
    // avoiding a full-memory hash/allocation each time the reference advances.
    uint32_t identity = checkpoint_hash(data, length);
    checkpoint_apply(k->baseline, data);
    k->baseline_hash = identity;
    // The same authoritative world is now an empty delta against its new base.
    // This lets the client defer restoration until its ordinary render update.
    checkpoint_u32(data + 8, identity);
    checkpoint_u32(data + 16, 0);
    checkpoint_u32(data + 20, checkpoint_hash(data + 24, 0));
    return 1;
}
