/*-
 * Copyright (c) 2006 Joseph Koshy
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ar.h>
#include <libelf.h>
#include <string.h>

#include "_libelf.h"

Elf *
elf_memory(char *image, size_t sz)
{
	Elf *e;

	if (LIBELF_PRIVATE(version) == EV_NONE) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		return (NULL);
	}

	if (image == NULL || sz == 0) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((e = _libelf_allocate_elf()) == NULL)
		return (NULL);

	e->e_cmd = ELF_C_READ;
	e->e_rawfile = image;
	e->e_rawsize = sz;

#undef	LIBELF_IS_ELF
#define	LIBELF_IS_ELF(P) ((P)[EI_MAG0] == ELFMAG0 && 		\
	(P)[EI_MAG1] == ELFMAG1 && (P)[EI_MAG2] == ELFMAG2 &&	\
	(P)[EI_MAG3] == ELFMAG3)

	if (sz > EI_NIDENT && LIBELF_IS_ELF(image)) {
		_libelf_init_elf(e, ELF_K_ELF);
		e->e_class = image[EI_CLASS];
		e->e_byteorder = image[EI_DATA];
		e->e_version = image[EI_VERSION];

		if (e->e_version > EV_CURRENT) {
			e = _libelf_release_elf(e);
			LIBELF_SET_ERROR(VERSION, 0);
			return (NULL);
		}

		if ((e->e_byteorder != ELFDATA2LSB && e->e_byteorder !=
 		    ELFDATA2MSB) || (e->e_class != ELFCLASS32 && e->e_class !=
		    ELFCLASS64)) {
			e = _libelf_release_elf(e);
			LIBELF_SET_ERROR(HEADER, 0);
			return (NULL);
		}

	} else if (sz >= SARMAG &&
	    strncmp(image, ARMAG, (size_t) SARMAG) == 0) {
		_libelf_init_elf(e, ELF_K_AR);
		e = _libelf_ar_open(e);
	} else
		_libelf_init_elf(e, ELF_K_NONE);

	return (e);
}