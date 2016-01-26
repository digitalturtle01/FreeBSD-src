/*-
 * Copyright (c) 2015 Eric McCorkle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <efi.h>

#include "boot_module.h"

#include "libzfs.h"
#include "zfsimpl.c"

static dev_info_t *devices;

static int
vdev_read(vdev_t *vdev, void *priv, off_t off, void *buf, size_t bytes)
{
	dev_info_t *devinfo;
	off_t lba;
	EFI_STATUS status;

	devinfo = (dev_info_t *)priv;
	lba = off / devinfo->dev->Media->BlockSize;

	status = devinfo->dev->ReadBlocks(devinfo->dev,
	    devinfo->dev->Media->MediaId, lba, bytes, buf);
	if (status != EFI_SUCCESS) {
		DPRINTF("vdev_read: failed dev: %p, id: %u, lba: %zu, size: %zu,"
                    " status: %lu\n", devinfo->dev,
                    devinfo->dev->Media->MediaId, lba, bytes,
                    EFI_ERROR_CODE(status));
		return (-1);
	}

	return (0);
}

static EFI_STATUS
probe(dev_info_t *dev)
{
	spa_t *spa;
	dev_info_t *tdev;
	EFI_STATUS status;

	/* ZFS consumes the dev on success so we need a copy. */
	if ((status = bs->AllocatePool(EfiLoaderData, sizeof(*dev),
	    (void**)&tdev)) != EFI_SUCCESS) {
		DPRINTF("Failed to allocate tdev (%lu)\n",
		    EFI_ERROR_CODE(status));
		return (status);
	}
	memcpy(tdev, dev, sizeof(*dev));

	if (vdev_probe(vdev_read, tdev, &spa) != 0) {
		(void)bs->FreePool(tdev);
		return (EFI_UNSUPPORTED);
	}

	dev->devdata = spa;
	add_device(&devices, dev);

	return (EFI_SUCCESS);
}

static EFI_STATUS
try_load(dev_info_t *devinfo, const char *loader_path, void **bufp, size_t *bufsize)
{
	spa_t *spa;
	struct zfsmount zfsmount;
	dnode_phys_t dn;
	struct stat st;
	int err;
	void *buf;
	EFI_STATUS status;

	spa = devinfo->devdata;
	if (zfs_spa_init(spa) != 0) {
		/* Init failed, don't report this loudly. */
		return (EFI_NOT_FOUND);
	}

	if (zfs_mount(spa, 0, &zfsmount) != 0) {
		/* Mount failed, don't report this loudly. */
		return (EFI_NOT_FOUND);
	}

	if ((err = zfs_lookup(&zfsmount, loader_path, &dn)) != 0) {
		printf("Failed to lookup %s on pool %s (%d)\n", loader_path,
		    spa->spa_name, err);
		return (EFI_INVALID_PARAMETER);
	}

	if ((err = zfs_dnode_stat(spa, &dn, &st)) != 0) {
		printf("Failed to lookup %s on pool %s (%d)\n", loader_path,
		    spa->spa_name, err);
		return (EFI_INVALID_PARAMETER);
	}

	if ((status = bs->AllocatePool(EfiLoaderData, (UINTN)st.st_size, &buf))
	    != EFI_SUCCESS) {
		printf("Failed to allocate load buffer for pool %s (%lu)\n",
		    spa->spa_name, EFI_ERROR_CODE(status));
		return (EFI_INVALID_PARAMETER);
	}

	if ((err = dnode_read(spa, &dn, 0, buf, st.st_size)) != 0) {
		printf("Failed to read node from %s (%d)\n", spa->spa_name,
		    err);
		(void)bs->FreePool(buf);
		return (EFI_INVALID_PARAMETER);
	}

	*bufsize = st.st_size;
	*bufp = buf;

	return (EFI_SUCCESS);
}

static EFI_STATUS
load(const char *loader_path, dev_info_t **devinfop, void **bufp,
    size_t *bufsize)
{
	dev_info_t *devinfo;
	EFI_STATUS status;

	for (devinfo = devices; devinfo != NULL; devinfo = devinfo->next) {
		status = try_load(devinfo, loader_path, bufp, bufsize);
		if (status == EFI_SUCCESS) {
			*devinfop = devinfo;
			return (EFI_SUCCESS);
		} else if (status != EFI_NOT_FOUND) {
			return (status);
		}
	}

	return (EFI_NOT_FOUND);
}

static void
status()
{
	spa_t *spa;

	spa = STAILQ_FIRST(&zfs_pools);
	if (spa == NULL) {
		printf("%s found no pools\n", zfs_module.name);
		return;
	}

	printf("%s found the following pools:", zfs_module.name);
	STAILQ_FOREACH(spa, &zfs_pools, spa_link)
		printf(" %s", spa->spa_name);

	printf("\n");
}

static void
init()
{

	zfs_init();
}

const boot_module_t zfs_module =
{
	.name = "ZFS",
	.init = init,
	.probe = probe,
	.load = load,
	.status = status
};