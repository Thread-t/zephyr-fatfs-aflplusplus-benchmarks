/* Standalone stand-in for zephyr_fatfs_config.h — hardcodes the resolved
 * values instead of pulling them from devicetree/Kconfig. */

#undef FF_FS_TINY
#define FF_FS_TINY 1

#undef FF_USE_MKFS
#define FF_USE_MKFS 1

#undef FF_CODE_PAGE
#define FF_CODE_PAGE 437

#undef FF_MIN_SS
#define FF_MIN_SS 512
#undef FF_MAX_SS
#define FF_MAX_SS 512

#undef FF_STR_VOLUME_ID
#define FF_STR_VOLUME_ID 1
#undef FF_VOLUME_STRS
#define FF_VOLUME_STRS "RAM"
#undef FF_VOLUMES
#define FF_VOLUMES 1

#undef FF_FS_NORTC
#define FF_FS_NORTC 1
