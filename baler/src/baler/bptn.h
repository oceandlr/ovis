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
/**
 * \file bptn.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup bptn Baler Pattern Management
 * \{
 * \brief Baler Pattern Management Utilities.
 * This module contains pattern storage (see ::bptn_store), which stores
 * patterns and their attributes. The information is managed in both pure
 * in-memory data structure and mapped-memory data structure for fast
 * serialization.
 *
 * \note A pattern is defined as a string of token IDs (the wildcard token is a
 * special token), utilizing the structure ::bstr. However, the pattern
 * storage only see patterns as simple ::bstr. Conversion between the two is
 * safe as they basically are the same structure \<byte_length, data\>, with
 * explicit data type for convenient access.
 */

#ifndef __BPTH_H
#define __BPTH_H

#include <pthread.h>

#include "bcommon.h"
#include "btypes.h"
#include "bset.h"
#include "bmvec.h"
#include "barray.h"
#include "bmapper.h"
#include "btkn.h"

/**
 * Attributes for a Baler Pattern (stored in ::bptn_store::mattr).
 */
struct bptn_attrM {
	uint32_t argc; /**< Number of arguments. */
	uint64_t arg_off[0]; /**< Offset to the argument list in
			   	  ::bptn_store::marg */
};

/**
 * In-memory pattern attribute.
 */
struct bptn_attr {
	uint32_t argc;
	struct bset_u32 arg[0]; /**< arg[i] is a set that hold the ith arg. */
};

/**
 * Convenient allocation function for ::bptn_attr.
 * \param argc The number of arguments for the pattern.
 */
struct bptn_attr* bptn_attr_alloc(uint32_t argc);

/**
 * Free the \a attr, along with all of the data owned by it.
 */
void bptn_attr_free(struct bptn_attr *attr);

/**
 * Definition for bvec_set_u32.
 */
BVEC_DEF(bvec_set_u32, struct bset_u32);

/**
 * Store for Baler Pattern.
 */
struct bptn_store {
	char *path; /**< The path of the store. */
	pthread_mutex_t mutex; /**< Write mutex. */
	struct bmem *marg; /**< ::bmem for arguments. */
	struct bmem *mattr; /**< ::bmem for attributes. */
	struct bmvec_u64 *attr_idx; /**< Index to attribute.
					attr_idx[ID] is the attribute of
					pattern ID. */
	struct barray *aattr; /**< aattr[ID] is the pointer to the
					in-memory attribute
				     	of patern ID (::bptn_attr). */
	struct bmap *map; /**< map STR\<--\>ID */
};

/**
 * Open ::bptn_store at path \a path, or initialize a new store
 * if it does not exist.
 * \return NULL on error.
 * \return The pointer to the opened ::bptn_store on success.
 */
struct bptn_store* bptn_store_open(const char *path);

/**
 * Close the \a store and free the structure (together with all data owned
 * by \a store).
 * \note The caller has to make sure that no one is using this store, otherwise
 * 	program may crash from segmentation fault.
 * \param store The pointer to ::bptn_store.
 */
void bptn_store_close_free(struct bptn_store *store);

/**
 * Add \a ptn into \a store. This is only a convenient wrapper function
 * for \a store->map.
 * \param ptn The pattern to be added.
 * \param store The pattern store.
 * \return An ID of the \a ptn if success.
 * \return BMAP_ID_ERR if error.
 */
static
uint32_t bptn_store_addptn(struct bptn_store *store, struct bstr *ptn)
{
	return bmap_insert(store->map, ptn);
}

/**
 * Similar to bptn_store_addptn(), but with specific \c id.
 * \param store Pattern store handle
 * \param ptn Pattern handle
 * \param id The pattern ID
 * \retval id if success.
 * \retval special_id One of the special IDs if error.
 */
static
uint32_t bptn_store_addptn_with_id(struct bptn_store *store, struct bstr *ptn,
		uint32_t id)
{
	return bmap_insert_with_id(store->map, ptn, id);
}

/**
 * Add \a msg (which is pattern + args) into \a store.
 * \param store The store.
 * \param msg The message.
 * \return 0 on success.
 * \return Error code on failure.
 */
int bptn_store_addmsg(struct bptn_store *store, struct bmsg *msg);

/**
 * Convert \c ptn_id to C string.
 *
 * \param store The pattern store.
 * \param ptn_id The pattern ID.
 * \param dest The buffer for the output string.
 * \param len The maximum length for \a dest.
 *
 * \returns Error code on error.
 * \returns 0 on success.
 */
int bptn_store_id2str(struct bptn_store *ptns, struct btkn_store *tkns,
		      uint32_t ptn_id, char *dest, int len);

/**
 * Last ID.
 * \param ptns The pattern store.
 * \returns Current last ID of the given \c ptns (the store).
 */
uint32_t bptn_store_last_id(struct bptn_store *ptns);

/**
 * First ID.
 * \param ptns The pattern store.
 * \returns Current first ID of the given \c ptns (the store).
 */
uint32_t bptn_store_first_id(struct bptn_store *ptns);

/**
 * Get pattern ID corresponding to the given \c ptn.
 *
 * \retval ptn_id The pattern ID of \c ptn.
 * \retval bmap_id_error_t If the given pattern is not found.
 */
static inline
uint32_t bptn_store_get_id(struct bptn_store *ptns, struct bstr *ptn)
{
	return bmap_get_id(ptns->map, ptn);
}
#endif

/**\}*/