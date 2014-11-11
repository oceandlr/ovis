/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "btkn.h"

const char *btkn_type_str[] = {
	BTKN_TYPE__LIST(, BENUM_STR)
};

btkn_type_t btkn_type(const char *str)
{
	return bget_str_idx(btkn_type_str, BTKN_TYPE_LAST, str);
}

struct btkn_store* btkn_store_open(char *path)
{
	char tmp[PATH_MAX];
	int __errno = 0; /* global errno might get reset in error handling path
			    */
	if (!bfile_exists(path)) {
		if (bmkdir_p(path, 0755) == -1)
			goto err0;
	}
	if (!bis_dir(path)) {
		__errno = EINVAL;
		goto err0;
	}
	struct btkn_store *ts = (typeof(ts)) malloc(sizeof(*ts));
	if (!ts) {
		__errno = errno;
		goto err0;
	}
	ts->path = strdup(path);
	if (!ts->path) {
		__errno = errno;
		goto err1;
	}
	sprintf(tmp, "%s/tkn_attr.bmvec", path);
	ts->attr = bmvec_generic_open(tmp);
	if (!ts->attr) {
		__errno = errno;
		goto err2;
	}
	sprintf(tmp, "%s/tkn.map", path);
	ts->map = bmap_open(tmp);
	if (!ts->map) {
		__errno = errno;
		goto err3;
	}
	return ts;
err3:
	bmvec_generic_close_free(ts->attr);
err2:
	free(ts->path);
err1:
	free(ts);
err0:
	errno = __errno;
	return NULL;
}

void btkn_store_close_free(struct btkn_store *s)
{
	bmvec_generic_close_free(s->attr);
	bmap_close_free(s->map);
}

int btkn_store_id2str(struct btkn_store *store, uint32_t id,
		      char *dest, int len)
{
	dest[0] = '\0'; /* first set dest to empty string */
	const struct bstr *bstr = bmap_get_bstr(store->map, id);
	if (!bstr)
		return ENOENT;
	if (len <= bstr->blen)
		return ENOMEM;
	strncpy(dest, bstr->cstr, bstr->blen);
	dest[bstr->blen] = '\0'; /* null-terminate the string */
	return 0;
}

uint32_t btkn_store_insert_cstr(struct btkn_store *store, const char *str,
							btkn_type_t type)
{
	int blen = strlen(str);
	if (!blen) {
		errno = EINVAL;
		return BMAP_ID_ERR;
	}
	struct bstr *bstr = malloc(sizeof(*bstr) + blen);
	bstr->blen = blen;
	memcpy(bstr->cstr, str, blen);
	uint32_t id = btkn_store_insert(store, bstr);
	if (id == BMAP_ID_ERR)
		return id;
	struct btkn_attr attr;
	attr.type = type;
	btkn_store_set_attr(store, id, attr);
	return id;
}

int btkn_store_char_insert(struct btkn_store *store, const char *cstr,
							btkn_type_t type)
{
	uint32_t buf[4];
	struct bstr *bs = (void*)buf;
	uint64_t tkn_id;
	bs->blen = 1;
	const char *c = cstr;
	struct btkn_attr attr;
	attr.type = type;
	while (*c) {
		bs->cstr[0] = *c;
		tkn_id = btkn_store_insert(store, bs);
		if (tkn_id == BMAP_ID_ERR)
			return errno;
		btkn_store_set_attr(store, tkn_id, attr);
		c++;
	}
}
