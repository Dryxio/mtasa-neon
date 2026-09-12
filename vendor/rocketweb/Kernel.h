#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct NeonRocketKernel NeonRocketKernel;
    NeonRocketKernel*               nrk_create(void);
    void                            nrk_destroy(NeonRocketKernel* kernel);
    /* Operations: configure(default/flat, opponent), step(ticks), reset(kickoff),
       clear goal, control ball(car, action), unlimited boost, reset camera. */
    int                  nrk_command(NeonRocketKernel* kernel, int operation, int a, int b);
    int                  nrk_controls(NeonRocketKernel* kernel, int car, const float values[8]);
    int                  nrk_state(NeonRocketKernel* kernel, float* output, size_t capacity);
    int                  nrk_pads(NeonRocketKernel* kernel, float* output, size_t capacity);
    int                  nrk_camera(NeonRocketKernel* kernel, const double input[28], double output[42]);
    int                  nrk_ball_grounded(NeonRocketKernel* kernel);
    const unsigned char* nrk_collision_data(size_t* length);
    const char*          nrk_error(NeonRocketKernel* kernel);
    /* Fixed-profile checkpoint: all WASM linear memory plus the stack global.
       Native function pointers are never serialized. Call prepare after configure/reset. */
    int    nrk_checkpoint_prepare(NeonRocketKernel* kernel);
    size_t nrk_checkpoint_save(NeonRocketKernel* kernel, unsigned char* output, size_t capacity);
    /* Advance the reference without modifying the live world. On success the
       first 24 bytes become a valid empty checkpoint against the new reference. */
    int nrk_checkpoint_rebase(NeonRocketKernel* kernel, unsigned char* data, size_t length);
    int nrk_checkpoint_restore(NeonRocketKernel* kernel, const unsigned char* data, size_t length);
#ifdef __cplusplus
}
#endif
