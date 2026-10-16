// mbelib built into OnAir (third_party/mbelib, ISC licence). The decoders still look for a user-installed mbelib first
// (DECT2_MBELIB_PATH, the codecs folders, the system), so a newer or different copy can override this one; when none is found they use
// these functions. Built only with -DDECT2_BUNDLE_MBELIB=ON (off by default: patented, users install mbelib themselves).
#pragma once
#ifdef DECT2_BUILTIN_MBE
extern "C" {
void mbe_initMbeParms(void* cur, void* prev, void* prevEnh);
void mbe_processImbe7200x4400Frame(short* aout, int* errs, int* errs2, char* errStr, char fr[8][23], char d[88], void* cur, void* prev, void* prevEnh, int uvq);
void mbe_processAmbe3600x2400Frame(short* aout, int* errs, int* errs2, char* errStr, char fr[4][24], char d[49], void* cur, void* prev, void* prevEnh, int uvq);
void mbe_processAmbe3600x2450Frame(short* aout, int* errs, int* errs2, char* errStr, char fr[4][24], char d[49], void* cur, void* prev, void* prevEnh, int uvq);
}
#endif
