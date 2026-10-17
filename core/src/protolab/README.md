# core/src/protolab: Protocol lab

The stages of the reverse-engineering workbench (View > Protocol lab): IQ files, bursts and demodulators (iq.cpp), baud rate and bit slicer (baud.cpp), line codings, preamble / sync and whitening (frame.cpp), CRC models and the CRC search (crc.cpp), field analysis (fields.cpp), the pipeline that tries the combinations (pipeline.cpp), exports (export.cpp) and the test signal generators (gen.cpp). Built as the small library dect2_protolab with no dependencies. See docs/protocol-lab.md.
