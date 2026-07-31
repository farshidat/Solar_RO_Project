#ifndef EVENT_CODES_H
#define EVENT_CODES_H

// Persistent (NVS) — E / W201 / W207 / L301
#define CODE_E101  "E101"  // Leak / hard leak lockout
#define CODE_E102  "E102"  // UV life expired
#define CODE_E103  "E103"  // Pre-filter expired
#define CODE_E104  "E104"  // Membrane 5/5 failed
#define CODE_E105  "E105"  // TDS UART fault
#define CODE_E106  "E106"  // RS485 Modbus fault
#define CODE_W201  "W201"  // Membrane warning (test start)
#define CODE_W207  "W207"  // Solar soiling / efficiency loss
#define CODE_L301  "L301"  // System reset executed
#define CODE_L302  "L302"  // Hard-lock unlock (برداشت قفل)

// Non-persistent (RAM only)
#define CODE_W204  "W204"  // Raw TDS1 high
#define CODE_W205  "W205"  // Still dirty post-flush
#define CODE_O301  "O301"  // Inlet low pressure purify pause
#define CODE_O302  "O302"  // Raw pump 30-min wait
#define CODE_O305  "O305"  // Raw water cleaned post-flush
#define CODE_O306  "O306"  // Leak cabinet 20-min dry-out

#define EVENT_CODE_LEN 8
#define EVENT_NVS_CAP  20
#define EVENT_RAM_CAP  32

#endif
