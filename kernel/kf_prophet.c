/* kf_prophet.c — Пророк как системный сервис (v1).
 *
 * Порт ядра Prophet-Lite (kenga-lang, PROPHET_LITE): эпизодическая память
 * паттернов + консолидация в «ядро» по важности + foresee — предсказание
 * следующего состояния системы по ближайшим эпизодам.
 *
 * ponytail v1: k-NN-ядро Пророка (без residual-MLP из lite — этап 2),
 * dim=8 фикс, статические массивы, без malloc. Алгоритмическая семья —
 * та же (surprise = RMSE, episodic→core consolidation).
 *
 * Событие системы (тик десктопа): [mouse_x, mouse_y, app, tick, kbd]
 * нормализуется в вектор; remember(last→obs); foresee(obs) предсказывает
 * следующее состояние. Десктоп рисует прогноз через kd_prophet_*.
 */
#include "kf_rt.h"

#define PP_DIM  8
#define PP_EP   32
#define PP_CORE 16

typedef struct {
    double pat[PP_DIM];
    double tgt[PP_DIM];
    double surprise;
} pp_ep;

typedef struct {
    double pat[PP_DIM];
    double tgt[PP_DIM];
    double importance;
} pp_core;

static pp_ep   eps[PP_EP];
static int     ep_n = 0, ep_i = 0;
static pp_core cores[PP_CORE];
static int     core_n = 0;

static double last_obs[PP_DIM];
static int    have_last = 0;
static double last_surprise = 0;

/* прогноз (заполняется на каждом тике) */
static double pred[PP_DIM];
static int    pred_valid = 0;
static int    pred_app = 0;
static int    pred_mx = 0, pred_my = 0;
static int    pred_surprise_pct = 0;
static uint64_t pred_count = 0, learn_count = 0;

static double pdist2(const double* a, const double* b) {
    double s = 0;
    for (int i = 0; i < PP_DIM; i++) { double d = a[i] - b[i]; s += d * d; }
    return s;
}

static void core_absorb(const pp_ep* e) {
    /* консолидация: эпизод вливается в ближайший элемент ядра
       (усреднение = «сон», важность растёт с повторениями) */
    int best = -1; double bd = 1e30;
    for (int i = 0; i < core_n; i++) {
        double d = pdist2(cores[i].pat, e->pat);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0 || bd > 0.25) {           /* далеко — новый элемент ядра */
        if (core_n < PP_CORE) {
            for (int i = 0; i < PP_DIM; i++) { cores[core_n].pat[i] = e->pat[i]; cores[core_n].tgt[i] = e->tgt[i]; }
            cores[core_n].importance = 1.0;
            core_n++;
        }
        return;
    }
    for (int i = 0; i < PP_DIM; i++) {
        cores[best].pat[i] = cores[best].pat[i] * 0.7 + e->pat[i] * 0.3;
        cores[best].tgt[i] = cores[best].tgt[i] * 0.7 + e->tgt[i] * 0.3;
    }
    cores[best].importance += 1.0;
}

static void remember(const double* pat, const double* tgt, double surprise) {
    pp_ep* e = &eps[ep_i];
    for (int i = 0; i < PP_DIM; i++) { e->pat[i] = pat[i]; e->tgt[i] = tgt[i]; }
    e->surprise = surprise;
    ep_i = (ep_i + 1) % PP_EP;
    if (ep_n < PP_EP) ep_n++;
    learn_count++;
    /* Consolidate: кольцо полное — выталкиваем старый эпизод в ядро */
    if (ep_n == PP_EP) {
        pp_ep* old = &eps[ep_i];   /* самый старый */
        core_absorb(old);
    }
}

static void foresee(const double* obs) {
    /* k-NN (k=4): взвешенное среднее tgt ближайших эпизодов и ядра */
    double acc[PP_DIM]; double wsum = 0;
    for (int i = 0; i < PP_DIM; i++) acc[i] = 0;
    for (int i = 0; i < ep_n; i++) {
        double d = pdist2(eps[i].pat, obs) + 1e-9;
        if (d > 4.0) continue;
        double w = 1.0 / d;
        wsum += w;
        for (int j = 0; j < PP_DIM; j++) acc[j] += w * eps[i].tgt[j];
    }
    for (int i = 0; i < core_n; i++) {
        double d = pdist2(cores[i].pat, obs) + 1e-9;
        if (d > 4.0) continue;
        double w = cores[i].importance / d;
        wsum += w;
        for (int j = 0; j < PP_DIM; j++) acc[j] += w * cores[i].tgt[j];
    }
    pred_valid = wsum > 0;
    if (pred_valid) for (int j = 0; j < PP_DIM; j++) pred[j] = acc[j] / wsum;
    else            for (int j = 0; j < PP_DIM; j++) pred[j] = obs[j];
}

static double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

/* --- системный тик: наблюдение → remember → foresee ---
   Вызывается из десктопа (kd_prophet_tick). mx,my — пиксели, app — окно. */
int64_t k_prophet_tick(int64_t mx, int64_t my, int64_t app, int64_t w, int64_t h) {
    double obs[PP_DIM];
    obs[0] = clamp01((double)mx / (w > 0 ? (double)w : 1.0));
    obs[1] = clamp01((double)my / (h > 0 ? (double)h : 1.0));
    obs[2] = clamp01((double)app);
    obs[3] = (double)((mx / 40) % 16) / 16.0;      /* сетка движения X */
    obs[4] = (double)((my / 40) % 16) / 16.0;      /* сетка движения Y */
    obs[5] = (double)(app % 4) / 4.0;
    obs[6] = 0.5;                                   /* резерв */
    obs[7] = 0.5;                                   /* резерв */

    if (have_last) {
        /* surprise: RMSE между прошлым наблюдением и текущим */
        double s = 0;
        for (int i = 0; i < PP_DIM; i++) { double d = last_obs[i] - obs[i]; s += d * d; }
        last_surprise = s / PP_DIM;
        remember(last_obs, obs, last_surprise);
        foresee(obs);
        pred_surprise_pct = (int)(last_surprise * 400.0);
        if (pred_surprise_pct > 100) pred_surprise_pct = 100;
        pred_app = (int)(pred[2] * 4.0 + 0.5);
        if (pred_app < 0) pred_app = 0;
        pred_mx = (int)(pred[0] * (w > 0 ? w : 800));
        pred_my = (int)(pred[1] * (h > 0 ? h : 600));
        pred_count++;
    } else {
        foresee(obs);
    }
    for (int i = 0; i < PP_DIM; i++) last_obs[i] = obs[i];
    have_last = 1;
    return (int64_t)pred_surprise_pct;
}

int64_t  k_prophet_pred_app(void)      { return pred_valid ? pred_app : -1; }
int64_t  k_prophet_pred_mx(void)       { return pred_mx; }
int64_t  k_prophet_pred_my(void)       { return pred_my; }
int64_t  k_prophet_surprise_pct(void)  { return pred_surprise_pct; }
int64_t  k_prophet_ep_count(void)      { return ep_n; }
int64_t  k_prophet_core_count(void)    { return core_n; }
int64_t  k_prophet_pred_count(void)    { return (int64_t)pred_count; }
int64_t  k_prophet_learn_count(void)   { return (int64_t)learn_count; }
