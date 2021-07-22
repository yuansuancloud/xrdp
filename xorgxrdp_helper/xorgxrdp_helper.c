
#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "arch.h"
#include "parse.h"
#include "trans.h"
#include "os_calls.h"
#include "log.h"

#include "xorgxrdp_helper.h"
#include "xorgxrdp_helper_x11.h"

#define XORGXRDP_SOURCE_XORG    1
#define XORGXRDP_SOURCE_XRDP    2

struct xorgxrdp_info
{
    struct trans *xorg_trans;
    struct trans *xrdp_trans;
    struct source_info si;
};

static int g_shmem_id_mapped = 0;
static int g_shmem_id = 0;
static void *g_shmem_pixels = 0;

/*****************************************************************************/
static int
xorg_process_message_61(struct xorgxrdp_info *xi, struct stream *s)
{
    int num_drects;
    int num_crects;
    int flags;
    int shmem_id;
    int shmem_offset;
    int width;
    int height;
    int cdata_bytes;
    int index;
    int error;
    struct xh_rect *crects;
    char *bmpdata;

    (void) xi;

    /* dirty pixels */
    in_uint16_le(s, num_drects);
    in_uint8s(s, 8 * num_drects);
    /* copied pixels */
    in_uint16_le(s, num_crects);
    crects = g_new(struct xh_rect, num_crects);
    for (index = 0; index < num_crects; index++)
    {
        in_uint16_le(s, crects[index].x);
        in_uint16_le(s, crects[index].y);
        in_uint16_le(s, crects[index].w);
        in_uint16_le(s, crects[index].h);
        //g_writeln("%d %d %d %d", crects[index * 4], crects[index * 4 + 1],
        //          crects[index * 4 + 2], crects[index * 4 + 3]);
    }
    in_uint32_le(s, flags);
    in_uint8s(s, 4); /* frame_id */
    in_uint32_le(s, shmem_id);
    in_uint32_le(s, shmem_offset);

    in_uint16_le(s, width);
    in_uint16_le(s, height);

    bmpdata = NULL;
    if (flags == 0) /* screen */
    {
        if (g_shmem_id_mapped == 0)
        {
            g_shmem_id = shmem_id;
            g_shmem_pixels = (char *) g_shmat(g_shmem_id);
            if (g_shmem_pixels == (void*)-1)
            {
                /* failed */
                g_shmem_id = 0;
                g_shmem_pixels = NULL;
                g_shmem_id_mapped = 0;
            }
            else
            {
                g_shmem_id_mapped = 1;
            }
        }
        else if (g_shmem_id != shmem_id)
        {
            g_shmem_id = shmem_id;
            g_shmdt(g_shmem_pixels);
            g_shmem_pixels = (char *) g_shmat(g_shmem_id);
            if (g_shmem_pixels == (void*)-1)
            {
                /* failed */
                g_shmem_id = 0;
                g_shmem_pixels = NULL;
                g_shmem_id_mapped = 0;
            }
        }
        if (g_shmem_pixels != NULL)
        {
            bmpdata = g_shmem_pixels + shmem_offset;
        }
    }
    if (bmpdata != NULL)
    {
        cdata_bytes = 16 * 1024 * 1024;
        error = xorgxrdp_helper_x11_encode_pixmap(width, height, 0,
                                                  num_crects, crects,
                                                  bmpdata + 4, &cdata_bytes);
        if (error != 0)
        {
            g_writeln("xorg_process_message_61: error %d", error);
        }
        ((int *) bmpdata)[0] = cdata_bytes;
    }
    g_free(crects);
    return 0;
}

/*****************************************************************************/
/* data going from xorg to xrdp */
static int
xorg_process_message(struct xorgxrdp_info *xi, struct stream *s)
{
    int type;
    int num;
    int size;
    int index;
    char *phold;
    int width;
    int height;
    int magic;
    int con_id;
    int mon_id;

    //g_writeln("xorg_process_message: %d bytes", (int) (s->end - s->data));
    in_uint16_le(s, type);
    in_uint16_le(s, num);
    in_uint32_le(s, size);
    //g_writeln("type %d num %d size %d", type, num, size);
    if (type == 3)
    {
        for (index = 0; index < num; index++)
        {
            phold = s->p;
            in_uint16_le(s, type);
            in_uint16_le(s, size);
            //g_writeln("xorg_process_message 3 type %d size %d", type, size);
            switch (type)
            {
                case 61:
                    xorg_process_message_61(xi, s);
                    break;
            }
            s->p = phold + size;
        }
    }
    else if (type == 100)
    {
        for (index = 0; index < num; index++)
        {
            phold = s->p;
            in_uint16_le(s, type);
            in_uint16_le(s, size);
            g_writeln("xorg_process_message 100 type %d size %d", type, size);
            switch (type)
            {
                case 1:
                    g_writeln("xorg_process_message: xorgxrdp_helper_x11_delete_all_pixmaps");
                    xorgxrdp_helper_x11_delete_all_pixmaps();
                    break;
                case 2:
                    in_uint16_le(s, width);
                    in_uint16_le(s, height);
                    in_uint32_le(s, magic);
                    in_uint32_le(s, con_id);
                    in_uint32_le(s, mon_id);
                    //width = (width + 15) & ~15;
                    //height = (height + 15) & ~15;
                    g_writeln("xorg_process_message: xorgxrdp_helper_x11_create_pixmap");
                    xorgxrdp_helper_x11_create_pixmap(width, height, magic, con_id, mon_id);
                    break;
            }
            s->p = phold + size;
        }
        return 0;
    }
    s->p = s->data;
    return trans_write_copy_s(xi->xrdp_trans, s);
}

/*****************************************************************************/
static int
xorg_data_in(struct trans* trans)
{
    struct stream *s;
    int len;
    struct xorgxrdp_info *xi;

    xi = (struct xorgxrdp_info *) (trans->callback_data);
    s = trans_get_in_s(trans);
    switch (trans->extra_flags)
    {
        case 1:
            s->p = s->data;
            in_uint8s(s, 4);
            in_uint32_le(s, len);
            if ((len < 0) || (len > 128 * 1024))
            {
                g_writeln("xorg_data_in: bad size %d", len);
                return 1;
            }
            if (len > 0)
            {
                trans->header_size = len + 8;
                trans->extra_flags = 2;
                break;
            }
            /* fall through */
        case 2:
            s->p = s->data;
            if (xorg_process_message(xi, s) != 0)
            {
                g_writeln("xorg_data_in: xorg_process_message failed");
                return 1;
            }
            init_stream(s, 0);
            trans->header_size = 8;
            trans->extra_flags = 1;
            break;
    }
    return 0;
}

/*****************************************************************************/
/* data going from xrdp to xorg */
static int
xrdp_process_message(struct xorgxrdp_info *xi, struct stream *s)
{
    //g_writeln("xrdp_process_message: %d bytes", (int) (s->end - s->data));
    return trans_write_copy_s(xi->xorg_trans, s);
}

/*****************************************************************************/
static int
xrdp_data_in(struct trans* trans)
{
    struct stream *s;
    int len;
    struct xorgxrdp_info *xi;

    xi = (struct xorgxrdp_info *) (trans->callback_data);
    s = trans_get_in_s(trans);
    switch (trans->extra_flags)
    {
        case 1:
            s->p = s->data;
            in_uint32_le(s, len);
            if ((len < 0) || (len > 128 * 1024))
            {
                g_writeln("xrdp_data_in: bad size %d", len);
                return 1;
            }
            if (len > 0)
            {
                trans->header_size = len;
                trans->extra_flags = 2;
                break;
            }
            /* fall through */
        case 2:
            s->p = s->data;
            if (xrdp_process_message(xi, s) != 0)
            {
                g_writeln("xrdp_data_in: xrdp_process_message failed");
                return 1;
            }
            init_stream(s, 0);
            trans->header_size = 4;
            trans->extra_flags = 1;
            break;
    }
    return 0;
}

/*****************************************************************************/
static void
sigpipe_func(int sig)
{
    (void) sig;
}

/*****************************************************************************/
int
main(int argc, char **argv)
{
    int xorg_fd;
    int xrdp_fd;
    int error;
    intptr_t robjs[16];
    int robj_count;
    intptr_t wobjs[16];
    int wobj_count;
    int timeout;
    struct xorgxrdp_info xi;
    //char text[64];

    if (argc < 2)
    {
        g_writeln("need to pass -d");
        return 0;
    }
    if (strcmp(argv[1], "-d") != 0)
    {
        g_writeln("need to pass -d");
        return 0;
    }
    g_init("xorgxrdp_helper");
    log_start(".xorgxrdp_helper.log", "xorgxrdp_helper");
    log_message(LOG_LEVEL_INFO, "xorgxrdp_helper startup");
#if 0
    g_file_close(1);
    g_file_close(2);
    g_snprintf(text, 63, "%s/.xorgxrdp_helper-%s-stdout.log",
               g_getenv("HOME"), g_getenv("DISPLAY"));
    text[63] = 0;
    g_file_open_ex(text, 1, 1, 1, 1);
    snprintf(text, 63, "%s/.xorgxrdp_helper-%s-stderr.log",
             g_getenv("HOME"), g_getenv("DISPLAY"));
    text[63] = 0;
    g_file_open_ex(text, 1, 1, 1, 1);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    g_memset(&xi, 0, sizeof(xi));
    g_writeln("main: in");
    g_signal_pipe(sigpipe_func);
    if (xorgxrdp_helper_x11_init() != 0)
    {
        g_writeln("xorgxrdp_helper_x11_init failed");
        return 0;
    }
    xorg_fd = g_atoi(g_getenv("XORGXRDP_XORG_FD"));
    xrdp_fd = g_atoi(g_getenv("XORGXRDP_XRDP_FD"));
    xi.xorg_trans = trans_create(TRANS_MODE_UNIX, 128 * 1024, 128 * 1024);
    xi.xorg_trans->sck = xorg_fd;
    xi.xorg_trans->status = TRANS_STATUS_UP;
    xi.xorg_trans->trans_data_in = xorg_data_in;
    xi.xorg_trans->header_size = 8;
    xi.xorg_trans->no_stream_init_on_data_in = 1;
    xi.xorg_trans->extra_flags = 1;
    xi.xorg_trans->callback_data = &xi;
    xi.xorg_trans->si = &(xi.si);
    xi.xorg_trans->my_source = XORGXRDP_SOURCE_XORG;
    xi.xrdp_trans = trans_create(TRANS_MODE_UNIX, 128 * 1024, 128 * 1024);
    xi.xrdp_trans->sck = xrdp_fd;
    xi.xrdp_trans->status = TRANS_STATUS_UP;
    xi.xrdp_trans->trans_data_in = xrdp_data_in;
    xi.xrdp_trans->no_stream_init_on_data_in = 1;
    xi.xrdp_trans->header_size = 4;
    xi.xrdp_trans->extra_flags = 1;
    xi.xrdp_trans->callback_data = &xi;
    xi.xrdp_trans->si = &(xi.si);
    xi.xrdp_trans->my_source = XORGXRDP_SOURCE_XRDP;
    for (;;)
    {
        robj_count = 0;
        wobj_count = 0;
        timeout = 0;
        error = trans_get_wait_objs_rw(xi.xorg_trans, robjs, &robj_count,
                                       wobjs, &wobj_count, &timeout);
        if (error != 0)
        {
            g_writeln("main: xorg trans_get_wait_objs_rw failed");
            break;
        }
        error = trans_get_wait_objs_rw(xi.xrdp_trans, robjs, &robj_count,
                                       wobjs, &wobj_count, &timeout);
        if (error != 0)
        {
            g_writeln("main: xrdp trans_get_wait_objs_rw failed");
            break;
        }
        error = xorgxrdp_helper_x11_get_wait_objs(robjs, &robj_count);
        if (error != 0)
        {
            g_writeln("main: xrdp xorgxrdp_helper_x11_get_wait_objs failed");
            break;
        }
        error = g_obj_wait(robjs, robj_count, wobjs, wobj_count, timeout);
        if (error != 0)
        {
            g_writeln("main: g_obj_wait failed");
            break;
        }
        error = trans_check_wait_objs(xi.xorg_trans);
        if (error != 0)
        {
            g_writeln("main: xorg trans_check_wait_objs failed");
            break;
        }
        error = trans_check_wait_objs(xi.xrdp_trans);
        if (error != 0)
        {
            g_writeln("main: xrdp trans_check_wait_objs failed");
            break;
        }
        error = xorgxrdp_helper_x11_check_wai_objs();
        if (error != 0)
        {
            g_writeln("main: xrdp xorgxrdp_helper_x11_check_wai_objs failed");
            break;
        }
    }
    g_writeln("main: out");
    return 0;
}
