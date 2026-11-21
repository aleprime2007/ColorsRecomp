#ifndef COLORSRECOMP_LOG_H
#define COLORSRECOMP_LOG_H

// Global host-side MMIO/IO trace switch, set by the --mmio-log CLI flag.
//
// Every device module prints its register traffic through this flag so a single
// switch turns the whole bus trace on without threading a context pointer
// through the MMIO callbacks (which mirror the GXRuntime device ABI).
extern int g_mmio_log;

#endif
