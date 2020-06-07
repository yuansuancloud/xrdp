/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2019
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * MS-RDPEGFX
 */

#ifndef _XRDP_EGFX_H
#define _XRDP_EGFX_H

#define XR_RDPGFX_CAPVERSION_8      0x00080004
#define XR_RDPGFX_CAPVERSION_81     0x00080105
#define XR_RDPGFX_CAPVERSION_10     0x000A0002
#define XR_RDPGFX_CAPVERSION_101    0x000A0100
#define XR_RDPGFX_CAPVERSION_102    0x000A0200
#define XR_RDPGFX_CAPVERSION_103    0x000A0301
#define XR_RDPGFX_CAPVERSION_104    0x000A0400
#define XR_RDPGFX_CAPVERSION_105    0x000A0502
#define XR_RDPGFX_CAPVERSION_106    0x000A0600 /* doc says 0x000A0601 */

#define XR_PIXEL_FORMAT_XRGB_8888   0x20
#define XR_PIXEL_FORMAT_ARGB_8888   0x21

#define XR_RDPGFX_CMDID_WIRETOSURFACE_1             0x0001
#define XR_RDPGFX_CMDID_WIRETOSURFACE_2             0x0002
#define XR_RDPGFX_CMDID_DELETEENCODINGCONTEXT       0x0003
#define XR_RDPGFX_CMDID_SOLIDFILL                   0x0004
#define XR_RDPGFX_CMDID_SURFACETOSURFACE            0x0005
#define XR_RDPGFX_CMDID_SURFACETOCACHE              0x0006
#define XR_RDPGFX_CMDID_CACHETOSURFACE              0x0007
#define XR_RDPGFX_CMDID_EVICTCACHEENTRY             0x0008
#define XR_RDPGFX_CMDID_CREATESURFACE               0x0009
#define XR_RDPGFX_CMDID_DELETESURFACE               0x000A
#define XR_RDPGFX_CMDID_STARTFRAME                  0x000B
#define XR_RDPGFX_CMDID_ENDFRAME                    0x000C
#define XR_RDPGFX_CMDID_FRAMEACKNOWLEDGE            0x000D
#define XR_RDPGFX_CMDID_RESETGRAPHICS               0x000E
#define XR_RDPGFX_CMDID_MAPSURFACETOOUTPUT          0x000F
#define XR_RDPGFX_CMDID_CACHEIMPORTOFFER            0x0010
#define XR_RDPGFX_CMDID_CACHEIMPORTREPLY            0x0011
#define XR_RDPGFX_CMDID_CAPSADVERTISE               0x0012
#define XR_RDPGFX_CMDID_CAPSCONFIRM                 0x0013
#define XR_RDPGFX_CMDID_MAPSURFACETOWINDOW          0x0015
#define XR_RDPGFX_CMDID_QOEFRAMEACKNOWLEDGE         0x0016
#define XR_RDPGFX_CMDID_MAPSURFACETOSCALEDOUTPUT    0x0017
#define XR_RDPGFX_CMDID_MAPSURFACETOSCALEDWINDOW    0x0018

struct xrdp_egfx_rect
{
    short x1;
    short y1;
    short x2;
    short y2;
};

struct xrdp_egfx_point
{
    short x;
    short y;
};

struct xrdp_egfx
{
    struct xrdp_session *session;
    int channel_id;
    int frame_id;
    struct stream *s;
    /* RDPGFX_CMDID_FRAMEACKNOWLEDGE */
    int queueDepth;
    int intframeId;
    int totalFramesDecoded;
    int pad0;
    /*  RDPGFX_CMDID_CAPSADVERTISE */
    int cap_version;
    int cap_flags;
};

struct xrdp_egfx *
xrdp_egfx_create(struct xrdp_mm *mm);
int
xrdp_egfx_delete(struct xrdp_egfx *egfx);

#endif
