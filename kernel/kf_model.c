/* kf_model.c — tiny neural net (MLP 2-2-1, sigmoid) as an in-kernel model.
 *
 * The "KengaOS 0.3 — Model Agent" goal: a neural computation hosted as a
 * system process. This is a small real MLP (same family as Kenga's
 * kenga_net.kenga). It is trained on XOR at init, so it actually predicts.
 * The model-agent process (kf_proc.c) runs it via IPC + CAP_MODEL_INFER.
 */
#include "kf_rt.h"

static double w1[4], b1[2];   /* hidden layer 2x2 + bias */
static double w2[2], b2[1];   /* output layer    2 + bias */

static double m_abs(double x) { return x < 0 ? -x : x; }
static double m_sig(double x) { return 0.5 + 0.5 * x / (1.0 + m_abs(x)); }
static double m_dsig(double x) { double u = 1.0 + m_abs(x); return 0.5 / (u * u); }

/* Train the 2-2-1 net on XOR (SGD, same equations as Kenga kenga_net.kenga). */
static void train_xor(int steps, double lr) {
    double xs[4][2] = {{0,0},{0,1},{1,0},{1,1}};
    double ts[4]   = {0,1,1,0};
    for (int s = 0; s < steps; s++) {
        int k = s & 3;
        double x0 = xs[k][0], x1 = xs[k][1], t = ts[k];
        double z0 = w1[0]*x0 + w1[1]*x1 + b1[0];
        double z1 = w1[2]*x0 + w1[3]*x1 + b1[1];
        double h0 = m_sig(z0), h1 = m_sig(z1);
        double zo = w2[0]*h0 + w2[1]*h1 + b2[0];
        double y  = m_sig(zo);
        double e  = y - t;
        double dy = e * m_dsig(zo);
        double ow0 = w2[0], ow1 = w2[1];
        w2[0] -= lr * dy * h0;  w2[1] -= lr * dy * h1;  b2[0] -= lr * dy;
        double dh0 = dy * ow0 * m_dsig(z0);
        double dh1 = dy * ow1 * m_dsig(z1);
        w1[0] -= lr * dh0 * x0;  w1[1] -= lr * dh0 * x1;
        w1[2] -= lr * dh1 * x0;  w1[3] -= lr * dh1 * x1;
        b1[0] -= lr * dh0;       b1[1] -= lr * dh1;
    }
}

int64_t k_model_init(void) {
    /* Proven init from Kenga's kenga_net.kenga (converges for XOR). */
    w1[0]=0.9;  w1[1]=-0.8; w1[2]=0.8; w1[3]=-0.9;
    b1[0]=0.2;  b1[1]=-0.2;
    w2[0]=1.4;  w2[1]=-1.4;
    b2[0]=0.0;
    train_xor(400, 0.35);
    return 1;
}

/* Infer: input two ints (0/1), returns rounded output (0 or 1).
   Compiled with -mno-sse so doubles use x87 (no 16-byte stack alignment
   requirement), safe to call from any stack alignment. */
int64_t k_model_infer(int64_t a, int64_t b) {
    double x0 = a ? 1.0 : 0.0, x1 = b ? 1.0 : 0.0;
    double h0 = m_sig(w1[0]*x0 + w1[1]*x1 + b1[0]);
    double h1 = m_sig(w1[2]*x0 + w1[3]*x1 + b1[1]);
    double y  = m_sig(w2[0]*h0 + w2[1]*h1 + b2[0]);
    return y >= 0.5 ? 1 : 0;
}

/* Debug: dump weights as hex bytes (so training can be inspected). */
static void m_hex(void* p, int n, char* out) {
    const char* hx = "0123456789abcdef";
    unsigned char* b = (unsigned char*)p;
    for (int i = 0; i < n; i++) { out[i*2] = hx[b[i]>>4]; out[i*2+1] = hx[b[i]&0xF]; }
}
int64_t k_model_dbg(void) {
    static char buf[7*16+1];
    char* o = buf;
    m_hex(&w1[0], 4, o); o += 8;   /* w1: 4 doubles -> 32 hex chars */
    m_hex(&w2[0], 2, o); o += 8;
    m_hex(&b1[0], 2, o); o += 8;
    m_hex(&b2[0], 1, o); o += 8;
    *o = 0;
    return (int64_t)(uintptr_t)buf;
}
