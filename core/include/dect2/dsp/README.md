# core/include/dect2/dsp: Signal processing building blocks

Public headers (#include "dect2/dsp/<name>.h") for this topic. FFT helpers and portable SIMD kernels (dsp_compat, simd, fftutil), resamplers, clock correction, LDPC decoders (CPU and GPU), IQ correction, spectrum, carrier finder, image processing and direction finding.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.
