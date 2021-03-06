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

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_vram.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"

#undef PSCNV_RB_AUGMENT

static void PSCNV_RB_AUGMENT(struct pscnv_vm_mapnode *node) {
	uint64_t maxgap = 0;
	struct pscnv_vm_mapnode *left = PSCNV_RB_LEFT(node, entry);
	struct pscnv_vm_mapnode *right = PSCNV_RB_RIGHT(node, entry);
	if (!node->vo)
		maxgap = node->size;
	if (left && left->maxgap > maxgap)
		maxgap = left->maxgap;
	if (right && right->maxgap > maxgap)
		maxgap = right->maxgap;
	node->maxgap = maxgap;
}

static int mapcmp(struct pscnv_vm_mapnode *a, struct pscnv_vm_mapnode *b) {
	if (a->start < b->start)
		return -1;
	else if (a->start > b->start)
		return 1;
	return 0;
}

PSCNV_RB_GENERATE_STATIC(pscnv_vm_maptree, pscnv_vm_mapnode, entry, mapcmp)

int pscnv_vspace_tlb_flush (struct pscnv_vspace *vs) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	int i, ret;
	for (i = 0; i < PSCNV_ENGINES_NUM; i++) {
		struct pscnv_engine *eng = dev_priv->engines[i];
		if (vs->engref[i])
			if ((ret = eng->tlb_flush(eng, vs)))
				return ret;
	}
	return 0;
}

struct pscnv_vspace *
pscnv_vspace_new (struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *res = kzalloc(sizeof *res, GFP_KERNEL);
	struct pscnv_vm_mapnode *fmap;
	if (!res) {
		NV_ERROR(dev, "VM: Couldn't alloc vspace\n");
		return 0;
	}
	res->dev = dev;
	kref_init(&res->ref);
	mutex_init(&res->lock);
	INIT_LIST_HEAD(&res->chan_list);
	PSCNV_RB_INIT(&res->maps);
	if (dev_priv->vm->do_vspace_new(res)) {
		kfree(res);
		return 0;
	}
	fmap = kzalloc(sizeof *fmap, GFP_KERNEL);
	if (!fmap) {
		NV_ERROR(dev, "VM: Couldn't alloc mapping\n");
		dev_priv->vm->do_vspace_free(res);
		kfree(res);
		return 0;
	}
	fmap->vspace = res;
	fmap->start = 0;
	fmap->size = 1ULL << 40;
	fmap->maxgap = fmap->size;
	PSCNV_RB_INSERT(pscnv_vm_maptree, &res->maps, fmap);
	return res;
}

void
pscnv_vspace_free(struct pscnv_vspace *vs) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	struct pscnv_vm_mapnode *node;
	while ((node = PSCNV_RB_ROOT(&vs->maps))) {
		if (node->vo && !vs->isbar) {
			drm_gem_object_unreference_unlocked(node->vo->gem);
		}
		PSCNV_RB_REMOVE(pscnv_vm_maptree, &vs->maps, node);
		kfree(node);
	}
	dev_priv->vm->do_vspace_free(vs);
	kfree(vs);
}

void pscnv_vspace_ref_free(struct kref *ref) {
	struct pscnv_vspace *vs = container_of(ref, struct pscnv_vspace, ref);
	int vid = vs->vid;
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;

	NV_INFO(vs->dev, "Freeing VSPACE %d\n", vid);

	pscnv_vspace_free(vs);

	dev_priv->vspaces[vid] = 0;
}

static struct pscnv_vm_mapnode *
pscnv_vspace_map_int(struct pscnv_vspace *vs, struct pscnv_vo *vo,
		uint64_t start, uint64_t end, int back,
		struct pscnv_vm_mapnode *node)
{
	struct pscnv_vm_mapnode *left, *right, *res;
	int lok, rok;
	uint64_t mstart, mend;
	left = PSCNV_RB_LEFT(node, entry);
	right = PSCNV_RB_RIGHT(node, entry);
	lok = left && left->maxgap >= vo->size && node->start > start;
	rok = right && right->maxgap >= vo->size && node->start + node->size  < end;
	if (pscnv_vm_debug >= 2)
		NV_INFO (vs->dev, "VM map: %llx %llx %llx %llx %llx %llx %llx %llx %llx %d %d\n", node->start, node->size, node->maxgap,
				left?left->start:0, left?left->size:0, left?left->maxgap:0,
				right?right->start:0, right?right->size:0, right?right->maxgap:0, lok, rok);
	if (!back && lok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, left);
		if (res)
			return res;
	}
	if (back && rok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, right);
		if (res)
			return res;
	}
	mstart = node->start;
	if (mstart < start)
		mstart = start;
	mend = node->start + node->size;
	if (mend > end)
		mend = end;
	if (mstart + vo->size <= mend && !node->vo) {
		if (back)
			mstart = mend - vo->size;
		mend = mstart + vo->size;
		if (node->start + node->size != mend) {
			struct pscnv_vm_mapnode *split = kzalloc(sizeof *split, GFP_KERNEL);
			if (!split)
				return 0;
			split->start = mend;
			split->size = node->start + node->size - mend;
			split->vspace = vs;
			node->size = mend - node->start;
			split->maxgap = split->size;
			PSCNV_RB_INSERT(pscnv_vm_maptree, &vs->maps, split);
		}
		if (node->start != mstart) {
			struct pscnv_vm_mapnode *split = kzalloc(sizeof *split, GFP_KERNEL);
			if (!split)
				return 0;
			split->start = node->start;
			split->size = mstart - node->start;
			split->vspace = vs;
			node->start = mstart;
			node->size = mend - node->start;
			split->maxgap = split->size;
			PSCNV_RB_INSERT(pscnv_vm_maptree, &vs->maps, split);
		}
		node->vo = vo;
		PSCNV_RB_AUGMENT(node);
		return node;
	}
	if (back && lok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, left);
		if (res)
			return res;
	}
	if (!back && rok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, right);
		if (res)
			return res;
	}
	return 0;
}

int
pscnv_vspace_map(struct pscnv_vspace *vs, struct pscnv_vo *vo,
		uint64_t start, uint64_t end, int back,
		struct pscnv_vm_mapnode **res)
{
	struct pscnv_vm_mapnode *node;
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	start += 0xfff;
	start &= ~0xfffull;
	end &= ~0xfffull;
	if (end > (1ull << 40))
		end = 1ull << 40;
	if (start >= end)
		return -EINVAL;
	mutex_lock(&vs->lock);
	node = pscnv_vspace_map_int(vs, vo, start, end, back, PSCNV_RB_ROOT(&vs->maps));
	if (!node) {
		mutex_unlock(&vs->lock);
		return -ENOMEM;
	}
	if (pscnv_vm_debug >= 1)
		NV_INFO(vs->dev, "Mapping VO %x/%d at %llx-%llx.\n", vo->cookie, vo->serial, node->start,
				node->start + node->size);
	dev_priv->vm->do_map(vs, vo, node->start);
	*res = node;
	mutex_unlock(&vs->lock);
	return 0;
}

static int
pscnv_vspace_unmap_node_unlocked(struct pscnv_vm_mapnode *node) {
	struct drm_nouveau_private *dev_priv = node->vspace->dev->dev_private;
	if (pscnv_vm_debug >= 1) {
		NV_INFO(node->vspace->dev, "Unmapping range %llx-%llx.\n", node->start, node->start + node->size);
	}
	dev_priv->vm->do_unmap(node->vspace, node->start, node->size);
	if (!node->vspace->isbar) {
		drm_gem_object_unreference(node->vo->gem);
	}
	node->vo = 0;
	node->maxgap = node->size;
	PSCNV_RB_AUGMENT(node);
	/* XXX: try merge */
	return 0;
}

int
pscnv_vspace_unmap_node(struct pscnv_vm_mapnode *node) {
	struct pscnv_vspace *vs = node->vspace;
	int ret;
	mutex_lock(&vs->lock);
	ret = pscnv_vspace_unmap_node_unlocked(node);
	mutex_unlock(&vs->lock);
	return ret;
}

int
pscnv_vspace_unmap(struct pscnv_vspace *vs, uint64_t start) {
	struct pscnv_vm_mapnode *node;
	int ret;
	mutex_lock(&vs->lock);
	node = PSCNV_RB_ROOT(&vs->maps);
	while (node) {
		if (node->start == start && node->vo) {
			ret = pscnv_vspace_unmap_node_unlocked(node);
			mutex_unlock(&vs->lock);
			return ret;
		}
		if (start < node->start)
			node = PSCNV_RB_LEFT(node, entry);
		else
			node = PSCNV_RB_RIGHT(node, entry);
	}
	mutex_unlock(&vs->lock);
	return -ENOENT;
}

static struct vm_operations_struct pscnv_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};	

int pscnv_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct pscnv_vo *vo;
	int ret;

	if (vma->vm_pgoff * PAGE_SIZE < (1ull << 31))
		return drm_mmap(filp, vma);

	if (vma->vm_pgoff * PAGE_SIZE < (1ull << 32))
		return pscnv_chan_mmap(filp, vma);

	obj = drm_gem_object_lookup(dev, priv, (vma->vm_pgoff * PAGE_SIZE) >> 32);
	if (!obj)
		return -ENOENT;
	vo = obj->driver_private;
	
	if (vma->vm_end - vma->vm_start > vo->size) {
		drm_gem_object_unreference_unlocked(obj);
		return -EINVAL;
	}
	if ((ret = dev_priv->vm->map_user(vo))) {
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}

	vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
	vma->vm_ops = &pscnv_vm_ops;
	vma->vm_private_data = obj;
	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

	vma->vm_file = filp;

	return remap_pfn_range(vma, vma->vm_start, 
			(dev_priv->fb_phys + vo->map1->start) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
}

/* needs vm_mutex held */
struct pscnv_vspace *
pscnv_get_vspace(struct drm_device *dev, struct drm_file *file_priv, int vid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (vid < 128 && vid >= 0 && dev_priv->vspaces[vid] && dev_priv->vspaces[vid]->filp == file_priv) {
		return dev_priv->vspaces[vid];
	}
	return 0;
}

int pscnv_ioctl_vspace_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_req *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid = -1;
	int i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	for (i = 0; i < 128; i++)
		if (!dev_priv->vspaces[i]) {
			vid = i;
			break;
		}

	if (vid == -1) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOSPC;
	}

	dev_priv->vspaces[vid] = pscnv_vspace_new(dev);
	if (!dev_priv->vspaces[i]) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOMEM;
	}

	dev_priv->vspaces[vid]->filp = file_priv;
	dev_priv->vspaces[vid]->vid = vid;
	
	req->vid = vid;

	NV_INFO(dev, "Allocating VSPACE %d\n", vid);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_vspace_free(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_req *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid = req->vid;
	struct pscnv_vspace *vs;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);
	vs = pscnv_get_vspace(dev, file_priv, vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	vs->filp = 0;
	kref_put(&vs->ref, pscnv_vspace_ref_free);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_vspace_map(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_map *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *vs;
	struct drm_gem_object *obj;
	struct pscnv_vo *vo;
	struct pscnv_vm_mapnode *map;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	vs = pscnv_get_vspace(dev, file_priv, req->vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	obj = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!obj) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -EBADF;
	}

	vo = obj->driver_private;

	ret = pscnv_vspace_map(vs, vo, req->start, req->end, req->back, &map);
	if (map)
		req->offset = map->start;

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}

int pscnv_ioctl_vspace_unmap(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_unmap *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *vs;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	vs = pscnv_get_vspace(dev, file_priv, req->vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	ret = pscnv_vspace_unmap(vs, req->offset);

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}

void pscnv_vspace_cleanup(struct drm_device *dev, struct drm_file *file_priv) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid;
	struct pscnv_vspace *vs;

	mutex_lock (&dev_priv->vm_mutex);
	for (vid = 0; vid < 128; vid++) {
		vs = pscnv_get_vspace(dev, file_priv, vid);
		if (!vs)
			continue;
		vs->filp = 0;
		kref_put(&vs->ref, pscnv_vspace_ref_free);
	}
	mutex_unlock (&dev_priv->vm_mutex);
}
