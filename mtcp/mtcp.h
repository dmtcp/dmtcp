//+++2006-01-17
//    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
//    EXPECT it to FAIL when someone's HeALTh or PROpeRTy is at RISk
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; version 2 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//---2006-01-17

#ifndef _MTCP_H
#define _MTCP_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif



int mtcp_init (char const *checkpointfilename, int interval, int clonenabledefault);
int mtcp_wrapper_clone (int (*fn) (void *arg), void *child_stack, int flags, void *arg);
int mtcp_ok (void);
int mtcp_no (void);

__attribute__ ((visibility ("hidden"))) void * mtcp_safemmap (void *start, size_t length, int prot, int flags, int fd, off_t offset);

void mtcp_set_callbacks(void (*sleep_between_ckpt)(int sec),
                        void (*pre_ckpt)(),
                        void (*post_ckpt)(int is_restarting));

#ifdef __cplusplus
}
#endif

#endif
