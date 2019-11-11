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
xrdp_egfx_send_capsconfirm(struct xrdp_egfx *egfx)
{
    LLOGLN(0, ("xrdp_egfx_send_capsconfirm:"));
    return 0;
}

/******************************************************************************/
static int
xrdp_egfx_process_capsadvertise(struct xrdp_egfx *egfx, struct stream *s)
{
    int index;
    int capsSetCount;
    int version;
    int capsDataLength;
    int flags;

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
        if ((version == 0x000a0600) && (flags == 0))
        {
            xrdp_egfx_send_capsconfirm(egfx);
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
            case 0x12: /* RDPGFX_CMDID_CAPSADVERTISE */
                error = xrdp_egfx_process_capsadvertise(egfx, s);
                break;
            default:
                LLOGLN(0, ("xrdp_egfx_process: unknown cmdId %d", cmdId));
                break;
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
                                 &procs, &(egfx->chanid));
    LLOGLN(0, ("xrdp_egfx_init: error %d egfx->chanid %d",
           error, egfx->chanid));
    return egfx;
}

/******************************************************************************/
int
xrdp_egfx_delete(struct xrdp_egfx *egfx)
{
    g_free(egfx);
    return 0;
}

