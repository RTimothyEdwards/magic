
/* 0.8 micron technology */
#define lambda_value 40
#include "calc.lambda"
#include "cifin-cmos26g.gen"
#define NOWELL
#include "cifin-cmos26g.gen"
#undef NOWELL

/* 0.8 micron technology */
#define lambda_value 30
#include "calc.lambda"
#include "cifin-cmos14b.gen"
#define NOWELL
#include "cifin-cmos14b.gen"
#undef NOWELL

/* 0.8 micron CMOSX technology */
#define lambda_value 40
#include "calc.lambda"
#include "cifin-cmosx.gen"
#define NOWELL
#include "cifin-cmosx.gen"
#undef NOWELL
