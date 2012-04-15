#ifndef __LIB_DEBUGF_H
#define __LIB_DEBUGF_H

//#define SHOW_HEX
//#define SHOW_DEBUG // comment out to turn off debug statements

#ifdef SHOW_DEBUG
#define DEBUGF(x, args ...) printf(x, args)
#endif

#ifndef DEBUGF
#define DEBUGF(x, args ...)
#endif

#endif
