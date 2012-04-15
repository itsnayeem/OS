#ifndef __LIB_DEBUGF_H
#define __LIB_DEBUGF_H

//#define SHOW_HEX
//#define SHOW_DEBUG // comment out to turn off debug statements
//#define SHOW_DEBUGA
//#define SHOW_DEBUGB
//#define SHOW_DEBUGC

#ifdef SHOW_DEBUG
#define DEBUGF(x, args ...) printf(x, args)
#endif

#ifdef SHOW_DEBUGA
#define DEBUGA(x, args ...) printf(x, args)
#endif

#ifdef SHOW_DEBUGB
#define DEBUGB(x, args ...) printf(x, args)
#endif

#ifdef SHOW_DEBUGC
#define DEBUGC(x, args ...) printf(x, args)
#endif

#ifndef DEBUGF
#define DEBUGF(x, args ...)
#endif

#ifndef DEBUGA
#define DEBUGA(x, args ...)
#endif

#ifndef DEBUGB
#define DEBUGB(x, args ...)
#endif

#ifndef DEBUGC
#define DEBUGC(x, args ...)
#endif

#endif
