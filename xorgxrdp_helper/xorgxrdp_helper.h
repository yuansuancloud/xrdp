
#ifndef _XORGXRDP_HELPER_H
#define _XORGXRDP_HELPER_H

#define XH_YUV420   1
#define XH_YUV444   2

#define XH_BT601    0
#define XF_BT709FR  1
#define XF_BTRFX    2

struct xh_rect
{
    short x;
    short y;
    short w;
    short h;
};

#endif
