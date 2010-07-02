/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef __PSCNV_FIFO_H__
#define __PSCNV_FIFO_H__

extern int pscnv_fifo_init(struct drm_device *);
extern int pscnv_fifo_takedown(struct drm_device *);
extern void pscnv_fifo_chan_free(struct pscnv_chan *ch);
int pscnv_ioctl_fifo_init(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_fifo_init_ib(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
void pscnv_fifo_irq_handler(struct drm_device *dev);

extern int nvc0_fifo_init(struct drm_device *);
extern int nvc0_fifo_takedown(struct drm_device *);
void nvc0_pfifo_irq_handler(struct drm_device *dev);
int nvc0_fifo_create(struct drm_device *dev, struct pscnv_chan *chan,
		     struct drm_pscnv_fifo_init *req);
extern void nvc0_fifo_chan_free(struct pscnv_chan *ch);

#endif
