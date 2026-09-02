#ifndef KENGA_MATH_SHIMM
#define KENGA_MATH_SHIMM
/* kf_rt: math.h не нужен — только static inline заглушки */
/* (кенга-ядро не использует libm; при добавлении float-математики расширить) */
static inline double sqrt(double x) { (void)x; return 0.0; }
static inline double sin(double x) { (void)x; return 0.0; }
static inline double cos(double x) { (void)x; return 0.0; }
#endif
