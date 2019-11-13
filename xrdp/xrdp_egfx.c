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

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch.h"
#include "os_calls.h"
#include "parse.h"
#include "xrdp.h"
#include "xrdp_egfx.h"

#define LLOG_LEVEL 1
#define LLOGLN(_level, _args) \
  do \
  { \
    if (_level < LLOG_LEVEL) \
    { \
        g_write("xrdp:xrdp_egfx [%10.10u]: ", g_time3()); \
        g_writeln _args ; \
    } \
  } \
  while (0)


/******************************************************************************/
static int
xrdp_egfx_send_data(struct xrdp_egfx *egfx, const char *data, int bytes)
{
    int error;
    int to_send;

    if (bytes <= 1500)
    {
        error = libxrdp_drdynvc_data(egfx->session, egfx->channel_id,
                                     data, bytes);
    }
    else
    {
        error = libxrdp_drdynvc_data_first(egfx->session, egfx->channel_id,
                                           data, 1500, bytes);
        data += 1500;
        bytes -= 1500;
        while ((bytes > 0) && (error == 0))
        {
            to_send = bytes;
            if (to_send > 1500)
            {
                to_send = 1500;
            }
            error = libxrdp_drdynvc_data(egfx->session, egfx->channel_id,
                                         data, to_send);
            data += to_send;
            bytes -= to_send;
        }
    }
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_send_create_surface(struct xrdp_egfx *egfx, int surface_id,
                              int width, int height, int pixel_format)
{
    int error;
    int bytes;
    struct stream *s;
    char *holdp;

    LLOGLN(0, ("xrdp_egfx_send_create_surface:"));
    make_stream(s);
    init_stream(s, 8192);
    /* RDP_SEGMENTED_DATA */
    out_uint8(s, 0xE0); /* descriptor = SINGLE */
    /* RDP8_BULK_ENCODED_DATA */
    out_uint8(s, 0x04); /* header = PACKET_COMPR_TYPE_RDP8 */
    /* RDPGFX_HEADER */
    out_uint16_le(s, 0x09); /* cmdId = RDPGFX_CMDID_CREATESURFACE */
    out_uint16_le(s, 0); /* flags = 0 */
    holdp = s->p;
    out_uint8s(s, 4); /* pduLength, set later */
    out_uint16_le(s, surface_id);
    out_uint16_le(s, width);
    out_uint16_le(s, height);
    out_uint8(s, pixel_format);
    s_mark_end(s);
    bytes = (int) ((s->end - holdp) + 4);
    s->p = holdp;
    out_uint32_le(s, bytes);
    bytes = (int) (s->end - s->data);
    error = xrdp_egfx_send_data(egfx, s->data, bytes);
    LLOGLN(0, ("xrdp_egfx_send_create_surface: xrdp_egfx_send_data error %d",
           error));
    free_stream(s);
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_send_map_surface(struct xrdp_egfx *egfx, int surface_id,
                           int x, int y)
{
    int error;
    int bytes;
    struct stream *s;
    char *holdp;

    LLOGLN(0, ("xrdp_egfx_send_map_surface:"));
    make_stream(s);
    init_stream(s, 8192);
    /* RDP_SEGMENTED_DATA */
    out_uint8(s, 0xE0); /* descriptor = SINGLE */
    /* RDP8_BULK_ENCODED_DATA */
    out_uint8(s, 0x04); /* header = PACKET_COMPR_TYPE_RDP8 */
    /* RDPGFX_HEADER */
    out_uint16_le(s, 0x0F); /* cmdId = RDPGFX_CMDID_MAPSURFACETOOUTPUT */
    out_uint16_le(s, 0); /* flags = 0 */
    holdp = s->p;
    out_uint8s(s, 4); /* pduLength, set later */
    out_uint16_le(s, surface_id);
    out_uint16_le(s, 0);
    out_uint32_le(s, x);
    out_uint32_le(s, y);
    s_mark_end(s);
    bytes = (int) ((s->end - holdp) + 4);
    s->p = holdp;
    out_uint32_le(s, bytes);
    bytes = (int) (s->end - s->data);
    error = xrdp_egfx_send_data(egfx, s->data, bytes);
    LLOGLN(0, ("xrdp_egfx_send_map_surface: xrdp_egfx_send_data error %d",
           error));
    free_stream(s);
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_send_fill_surface(struct xrdp_egfx *egfx, int surface_id,
                            int fill_color, int num_rects,
                            const struct xrdp_egfx_rect *rects)
{
    int error;
    int bytes;
    int index;
    struct stream *s;
    char *holdp;

    LLOGLN(0, ("xrdp_egfx_send_fill_surface:"));
    make_stream(s);
    init_stream(s, 8192);
    /* RDP_SEGMENTED_DATA */
    out_uint8(s, 0xE0); /* descriptor = SINGLE */
    /* RDP8_BULK_ENCODED_DATA */
    out_uint8(s, 0x04); /* header = PACKET_COMPR_TYPE_RDP8 */
    /* RDPGFX_HEADER */
    out_uint16_le(s, 0x04); /* cmdId = RDPGFX_CMDID_SOLIDFILL */
    out_uint16_le(s, 0); /* flags = 0 */
    holdp = s->p;
    out_uint8s(s, 4); /* pduLength, set later */
    out_uint16_le(s, surface_id);
    out_uint32_le(s, fill_color);
    out_uint16_le(s, num_rects);
    for (index = 0; index < num_rects; index++)
    {
        out_uint16_le(s, rects[index].x1);
        out_uint16_le(s, rects[index].y1);
        out_uint16_le(s, rects[index].x2);
        out_uint16_le(s, rects[index].y2);
    }
    s_mark_end(s);
    bytes = (int) ((s->end - holdp) + 4);
    s->p = holdp;
    out_uint32_le(s, bytes);
    bytes = (int) (s->end - s->data);
    error = xrdp_egfx_send_data(egfx, s->data, bytes);
    LLOGLN(0, ("xrdp_egfx_send_fill_surface: xrdp_egfx_send_data error %d",
           error));
    free_stream(s);
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_send_surface_to_surface(struct xrdp_egfx *egfx, int src_surface_id,
                                  int dst_surface_id,
                                  const struct xrdp_egfx_rect *src_rect,
                                  int num_dst_points,
                                  const struct xrdp_egfx_point *dst_points)
{
    int error;
    int bytes;
    int index;
    struct stream *s;
    char *holdp;

    LLOGLN(0, ("xrdp_egfx_send_surface_to_surface:"));
    make_stream(s);
    init_stream(s, 8192);
    /* RDP_SEGMENTED_DATA */
    out_uint8(s, 0xE0); /* descriptor = SINGLE */
    /* RDP8_BULK_ENCODED_DATA */
    out_uint8(s, 0x04); /* header = PACKET_COMPR_TYPE_RDP8 */
    /* RDPGFX_HEADER */
    out_uint16_le(s, 0x05); /* cmdId = RDPGFX_CMDID_SURFACETOSURFACE */
    out_uint16_le(s, 0); /* flags = 0 */
    holdp = s->p;
    out_uint8s(s, 4); /* pduLength, set later */
    out_uint16_le(s, src_surface_id);
    out_uint16_le(s, dst_surface_id);
    out_uint16_le(s, src_rect->x1);
    out_uint16_le(s, src_rect->y1);
    out_uint16_le(s, src_rect->x2);
    out_uint16_le(s, src_rect->y2);
    out_uint16_le(s, num_dst_points);
    for (index = 0; index < num_dst_points; index++)
    {
        out_uint16_le(s, dst_points[index].x);
        out_uint16_le(s, dst_points[index].y);
    }
    s_mark_end(s);
    bytes = (int) ((s->end - holdp) + 4);
    s->p = holdp;
    out_uint32_le(s, bytes);
    bytes = (int) (s->end - s->data);
    error = xrdp_egfx_send_data(egfx, s->data, bytes);
    LLOGLN(0, ("xrdp_egfx_send_surface_to_surface: "
           "xrdp_egfx_send_data error %d", error));
    free_stream(s);
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_send_frame_start(struct xrdp_egfx *egfx, int frame_id, int timestamp)
{
    int error;
    int bytes;
    struct stream *s;
    char *holdp;

    LLOGLN(0, ("xrdp_egfx_send_frame_start:"));
    make_stream(s);
    init_stream(s, 8192);
    /* RDP_SEGMENTED_DATA */
    out_uint8(s, 0xE0); /* descriptor = SINGLE */
    /* RDP8_BULK_ENCODED_DATA */
    out_uint8(s, 0x04); /* header = PACKET_COMPR_TYPE_RDP8 */
    /* RDPGFX_HEADER */
    out_uint16_le(s, 0x0B); /* cmdId = RDPGFX_CMDID_STARTFRAME */
    out_uint16_le(s, 0); /* flags = 0 */
    holdp = s->p;
    out_uint8s(s, 4); /* pduLength, set later */
    out_uint32_le(s, timestamp);
    out_uint32_le(s, frame_id);
    s_mark_end(s);
    bytes = (int) ((s->end - holdp) + 4);
    s->p = holdp;
    out_uint32_le(s, bytes);
    bytes = (int) (s->end - s->data);
    error = xrdp_egfx_send_data(egfx, s->data, bytes);
    LLOGLN(0, ("xrdp_egfx_send_frame_start: xrdp_egfx_send_data error %d",
           error));
    free_stream(s);
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_send_frame_end(struct xrdp_egfx *egfx, int frame_id)
{
    int error;
    int bytes;
    struct stream *s;
    char *holdp;

    LLOGLN(0, ("xrdp_egfx_send_frame_end:"));
    make_stream(s);
    init_stream(s, 8192);
    /* RDP_SEGMENTED_DATA */
    out_uint8(s, 0xE0); /* descriptor = SINGLE */
    /* RDP8_BULK_ENCODED_DATA */
    out_uint8(s, 0x04); /* header = PACKET_COMPR_TYPE_RDP8 */
    /* RDPGFX_HEADER */
    out_uint16_le(s, 0x0C); /* cmdId = RDPGFX_CMDID_ENDFRAME */
    out_uint16_le(s, 0); /* flags = 0 */
    holdp = s->p;
    out_uint8s(s, 4); /* pduLength, set later */
    out_uint32_le(s, frame_id);
    s_mark_end(s);
    bytes = (int) ((s->end - holdp) + 4);
    s->p = holdp;
    out_uint32_le(s, bytes);
    bytes = (int) (s->end - s->data);
    error = xrdp_egfx_send_data(egfx, s->data, bytes);
    LLOGLN(0, ("xrdp_egfx_send_frame_end: xrdp_egfx_send_data error %d",
           error));
    free_stream(s);
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_send_capsconfirm(struct xrdp_egfx *egfx)
{
    int error;
    int bytes;
    struct stream *s;
    char *holdp;

    LLOGLN(0, ("xrdp_egfx_send_capsconfirm:"));
    make_stream(s);
    init_stream(s, 8192);
    /* RDP_SEGMENTED_DATA */
    out_uint8(s, 0xE0); /* descriptor = SINGLE */
    /* RDP8_BULK_ENCODED_DATA */
    out_uint8(s, 0x04); /* header = PACKET_COMPR_TYPE_RDP8 */
    /* RDPGFX_HEADER */
    out_uint16_le(s, 0x13); /* cmdId = RDPGFX_CMDID_CAPSCONFIRM */
    out_uint16_le(s, 0); /* flags = 0 */
    holdp = s->p;
    out_uint8s(s, 4); /* pduLength, set later */
    out_uint32_le(s, 0x000A0400); /* version = RDPGFX_CAPVERSION_104 */
    out_uint32_le(s, 4); /* capsDataLength */
    out_uint8s(s, 4);
    s_mark_end(s);
    bytes = (int) ((s->end - holdp) + 4);
    s->p = holdp;
    out_uint32_le(s, bytes);
    bytes = (int) (s->end - s->data);
    error = xrdp_egfx_send_data(egfx, s->data, bytes);
    LLOGLN(0, ("xrdp_egfx_send_capsconfirm: xrdp_egfx_send_data error %d",
           error));
    free_stream(s);
    return error;
}

/******************************************************************************/
/* RDPGFX_CMDID_FRAMEACKNOWLEDGE */
static int
xrdp_egfx_process_frame_ack(struct xrdp_egfx *egfx, struct stream *s)
{
    LLOGLN(0, ("xrdp_egfx_process_frame_ack:"));
    if (!s_check_rem(s, 12))
    {
        return 1;
    }
    in_uint32_le(s, egfx->queueDepth);
    in_uint32_le(s, egfx->intframeId);
    in_uint32_le(s, egfx->totalFramesDecoded);
    LLOGLN(0, ("xrdp_egfx_process_frame_ack: queueDepth %d intframeId %d "
           "totalFramesDecoded %d",
           egfx->queueDepth, egfx->intframeId, egfx->totalFramesDecoded));
    return 0;
}

/******************************************************************************/
/* RDPGFX_CMDID_CAPSADVERTISE */
static int
xrdp_egfx_process_capsadvertise(struct xrdp_egfx *egfx, struct stream *s)
{
    int index;
    int capsSetCount;
    int version;
    int capsDataLength;
    int flags;
    struct xrdp_egfx_rect rect;
    struct xrdp_egfx_point point;

    LLOGLN(0, ("xrdp_egfx_process_capsadvertise:"));
    in_uint16_le(s, capsSetCount);
    for (index = 0; index < capsSetCount; index++)
    {
        if (!s_check_rem(s, 8))
        {
            return 1;
        }
        in_uint32_le(s, version);
        in_uint32_le(s, capsDataLength);
        if (!s_check_rem(s, capsDataLength))
        {
            return 1;
        }
        if (capsDataLength != 4)
        {
            return 1;
        }
        in_uint32_le(s, flags);
        LLOGLN(0, ("xrdp_egfx_process_capsadvertise: version 0x%8.8x "
               "capsDataLength %d flags 0x%8.8x",
               version, capsDataLength, flags));
        if ((version == 0x000A0400) && (flags == 0))
        {
            xrdp_egfx_send_capsconfirm(egfx);
            xrdp_egfx_send_create_surface(egfx, 1, 1920, 1080, 0x20);
            xrdp_egfx_send_map_surface(egfx, 1, 0, 0);
            xrdp_egfx_send_create_surface(egfx, 2, 100, 100, 0x20);
            rect.x1 = 0;
            rect.y1 = 0;
            rect.x2 = 100;
            rect.y2 = 100;
            xrdp_egfx_send_fill_surface(egfx, 2, 0x0000FF00, 1, &rect);
            egfx->frame_id++;
            xrdp_egfx_send_frame_start(egfx, egfx->frame_id, 0);
            point.x = 200;
            point.y = 200;
            xrdp_egfx_send_surface_to_surface(egfx, 2, 1, &rect, 1, &point);
            xrdp_egfx_send_frame_end(egfx, egfx->frame_id);
            egfx->frame_id++;
            xrdp_egfx_send_frame_start(egfx, egfx->frame_id, 0);
            point.x = 400;
            point.y = 400;
            xrdp_egfx_send_surface_to_surface(egfx, 2, 1, &rect, 1, &point);
            xrdp_egfx_send_frame_end(egfx, egfx->frame_id);
        }
    }
    return 0;
}

/******************************************************************************/
static int
xrdp_egfx_process(struct xrdp_egfx *egfx, struct stream *s)
{
    int error;
    int cmdId;
    int flags;
    int pduLength;
    char *holdp;
    char *holdend;

    LLOGLN(0, ("xrdp_egfx_process:"));
    error = 0;
    while (s_check_rem(s, 8))
    {
        in_uint16_le(s, cmdId);
        in_uint16_le(s, flags);
        in_uint32_le(s, pduLength);
        holdp = s->p;
        holdend = s->end;
        s->end = s->p + pduLength;
        LLOGLN(0, ("xrdp_egfx_process: cmdId %d flags %d pduLength %d",
               cmdId, flags, pduLength));
        if (pduLength < 8)
        {
            return 1;
        }
        if (!s_check_rem(s, pduLength - 8))
        {
            return 1;
        }
        switch (cmdId)
        {
            case 0x0D: /* RDPGFX_CMDID_FRAMEACKNOWLEDGE */
                error = xrdp_egfx_process_frame_ack(egfx, s);
                break;
            case 0x12: /* RDPGFX_CMDID_CAPSADVERTISE */
                error = xrdp_egfx_process_capsadvertise(egfx, s);
                break;
            default:
                LLOGLN(0, ("xrdp_egfx_process: unknown cmdId %d", cmdId));
                break;
        }
        if (error != 0)
        {
            return error;
        }
        s->p = holdp + pduLength; 
        s->end = holdend;
    }
    return error;
}

/******************************************************************************/
static int
xrdp_egfx_open_response(intptr_t id, int chan_id, int creation_status)
{
    LLOGLN(0, ("xrdp_egfx_open_response:"));
    return 0;
}

/******************************************************************************/
static int
xrdp_egfx_close_response(intptr_t id, int chan_id)
{
    LLOGLN(0, ("xrdp_egfx_close_response:"));
    return 0;
}

/******************************************************************************/
static int
xrdp_egfx_data_first(intptr_t id, int chan_id, char *data, int bytes,
                     int total_bytes)
{
    struct xrdp_process *process;
    struct xrdp_egfx *egfx;

    LLOGLN(0, ("xrdp_egfx_data_first:"));
    process = (struct xrdp_process *) id;
    egfx = process->wm->mm->egfx;
    if (egfx->s != NULL)
    {
        LLOGLN(0, ("xrdp_egfx_data_first: error"));
    }
    make_stream(egfx->s);
    init_stream(egfx->s, total_bytes);
    out_uint8a(egfx->s, data, bytes);
    return 0;
}

/******************************************************************************/
static int
xrdp_egfx_data(intptr_t id, int chan_id, char *data, int bytes)
{
    int error;
    struct stream ls;
    struct xrdp_process *process;
    struct xrdp_egfx *egfx;

    LLOGLN(0, ("xrdp_egfx_data:"));
    process = (struct xrdp_process *) id;
    egfx = process->wm->mm->egfx;
    if (egfx->s == NULL)
    {
        g_memset(&ls, 0, sizeof(ls));
        ls.data = data;
        ls.size = bytes;
        ls.p = data;
        ls.end = data + bytes;
        return xrdp_egfx_process(egfx, &ls);
    }
    if (!s_check_rem_out(egfx->s, bytes))
    {
        return 1;
    }
    out_uint8a(egfx->s, data, bytes);
    if (!s_check_rem_out(egfx->s, 1))
    {
        s_mark_end(egfx->s);
        error = xrdp_egfx_process(egfx, egfx->s);
        free_stream(egfx->s);
        egfx->s = NULL;
        return error;
    }
    return 1;
}

/******************************************************************************/
struct xrdp_egfx *
xrdp_egfx_create(struct xrdp_mm *mm)
{
    int error;
    struct xrdp_drdynvc_procs procs;
    struct xrdp_egfx *egfx;
    struct xrdp_process *process;

    egfx = g_new0(struct xrdp_egfx, 1);
    if (egfx == NULL)
    {
        return NULL;
    }
    procs.open_response = xrdp_egfx_open_response; 
    procs.close_response = xrdp_egfx_close_response;
    procs.data_first = xrdp_egfx_data_first;
    procs.data = xrdp_egfx_data;
    process = mm->wm->pro_layer;
    error = libxrdp_drdynvc_open(process->session,
                                 "Microsoft::Windows::RDS::Graphics",
                                 1, /* WTS_CHANNEL_OPTION_DYNAMIC */
                                 &procs, &(egfx->channel_id));
    LLOGLN(0, ("xrdp_egfx_init: error %d egfx->channel_id %d",
           error, egfx->channel_id));
    egfx->session = process->session;
    return egfx;
}

/******************************************************************************/
int
xrdp_egfx_delete(struct xrdp_egfx *egfx)
{
    g_free(egfx);
    return 0;
}

