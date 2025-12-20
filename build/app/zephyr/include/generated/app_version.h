#ifndef _APP_VERSION_H_
#define _APP_VERSION_H_

/*  values come from cmake/version.cmake
 * BUILD_VERSION related  values will be 'git describe',
 * alternatively user defined BUILD_VERSION.
 */

/* #undef ZEPHYR_VERSION_CODE */
/* #undef ZEPHYR_VERSION */

#define APPVERSION                   0xe0000
#define APP_VERSION_NUMBER           0xe00
#define APP_VERSION_MAJOR            0
#define APP_VERSION_MINOR            14
#define APP_PATCHLEVEL               0
#define APP_TWEAK                    0
#define APP_VERSION_STRING           "0.14.0"
#define APP_VERSION_EXTENDED_STRING  "0.14.0+0"
#define APP_VERSION_TWEAK_STRING     "0.14.0+0"

#define APP_BUILD_VERSION 9071569a348d


#endif /* _APP_VERSION_H_ */
