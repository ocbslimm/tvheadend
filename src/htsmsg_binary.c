/*
 *  Functions converting HTSMSGs to/from a simple binary format
 *  Copyright (C) 2007 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "htsmsg_binary.h"
#include "memoryinfo.h"

/*
 *
 */
static int
htsmsg_binary_des0(htsmsg_t *msg, const uint8_t *buf, size_t len)
{
  uint_fast32_t type, namelen, datalen;
  size_t tlen, nlen;
  htsmsg_field_t *f;
  htsmsg_t *sub;
  uint64_t u64;
  int i, bin = 0;

  while(len > 5) {

    type    =  buf[0];
    namelen =  buf[1];
    datalen = (((uint_fast32_t)buf[2]) << 24) |
              (((uint_fast32_t)buf[3]) << 16) |
              (((uint_fast32_t)buf[4]) << 8 ) |
              (((uint_fast32_t)buf[5])      );
    
    buf += 6;
    len -= 6;
    
    if(len < namelen + datalen)
      return -1;

    nlen = namelen ? htsmsg_malloc_align(type, namelen + 1) : 0;
    tlen = sizeof(htsmsg_field_t) + nlen;
    if (type == HMF_STR) {
      tlen += datalen + 1;
    } else if (type == HMF_LIST || type == HMF_MAP) {
      tlen += sizeof(htsmsg_t);
    } else if (type == HMF_UUID) {
      tlen = UUID_BIN_SIZE;
      if (tlen != datalen)
        return -1;
    }
    f = malloc(tlen);
    if (f == NULL)
      return -1;
#if ENABLE_SLOW_MEMORYINFO
    f->hmf_edata_size = tlen - sizeof(htsmsg_field_t);
    memoryinfo_alloc(&htsmsg_field_memoryinfo, tlen);
#endif
    f->hmf_type  = type;
    f->hmf_flags = 0;

    if(namelen > 0) {
      memcpy((char *)f->_hmf_name, buf, namelen);
      ((char *)f->_hmf_name)[namelen] = 0;

      buf += namelen;
      len -= namelen;
    } else {
      f->hmf_flags = HMF_NONAME;
    }

    switch(type) {
    case HMF_STR:
      f->hmf_str = f->_hmf_name + nlen;
      memcpy((char *)f->hmf_str, buf, datalen);
      ((char *)f->hmf_str)[datalen] = 0;
      f->hmf_flags |= HMF_INALLOCED;
      break;

    case HMF_UUID:
      f->hmf_uuid = (uint8_t *)f->_hmf_name + nlen;
      memcpy((char *)f->hmf_uuid, buf, UUID_BIN_SIZE);
      break;

    case HMF_BIN:
      f->hmf_bin = (const void *)buf;
      f->hmf_binsize = datalen;
      bin = 1;
      break;

    case HMF_S64:
      u64 = 0;
      for(i = datalen - 1; i >= 0; i--)
	  u64 = (u64 << 8) | buf[i];
      f->hmf_s64 = u64;
      break;

    case HMF_MAP:
    case HMF_LIST:
      sub = f->hmf_msg = (htsmsg_t *)(f->_hmf_name + nlen);
      TAILQ_INIT(&sub->hm_fields);
      sub->hm_data = NULL;
      sub->hm_data_size = 0;
      sub->hm_islist = type == HMF_LIST;
      i = htsmsg_binary_des0(sub, buf, datalen);
      if (i < 0) {
#if ENABLE_SLOW_MEMORYINFO
        memoryinfo_free(&htsmsg_field_memoryinfo, tlen);
#endif
        free(f);
        return -1;
      }
      if (i > 0)
        bin = 1;
      break;

    case HMF_BOOL:
      f->hmf_bool = datalen == 1 ? buf[0] : 0;
      break;

    default:
#if ENABLE_SLOW_MEMORYINFO
      memoryinfo_free(&htsmsg_field_memoryinfo, tlen);
#endif
      free(f);
      return -1;
    }

    TAILQ_INSERT_TAIL(&msg->hm_fields, f, hmf_link);
    buf += datalen;
    len -= datalen;
  }
  return len ? -1 : bin;
}

/*
 *
 */
htsmsg_t *
htsmsg_binary_deserialize0(const void *data, size_t len, const void *buf)
{
  htsmsg_t *msg;
  int r;

  msg = htsmsg_create_map();
  r = htsmsg_binary_des0(msg, data, len);
  if (r < 0) {
    free((void *)buf);
    htsmsg_destroy(msg);
    return NULL;
  }
  if (r > 0) {
    msg->hm_data = buf;
    msg->hm_data_size = len;
#if ENABLE_SLOW_MEMORYINFO
    memoryinfo_append(&htsmsg_memoryinfo, len);
#endif
  } else {
    free((void *)buf);
  }
  return msg;
}

/*
 *
 */
int
htsmsg_binary_deserialize(htsmsg_t **msg, const void *data, size_t *len, const void *buf)
{
  htsmsg_t *m;
  const uint8_t *p;
  uint32_t l, len2;

  len2 = *len;
  *msg = NULL;
  *len = 0;
  if (len2 < 4) {
    free((void *)buf);
    return -1;
  }
  p = data;
  l = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  if (l + 4 > len2) {
    free((void *)buf);
    return -1;
  }
  m = htsmsg_binary_deserialize0(data + 4, l, buf);
  if (m == NULL)
    return -1;
  *len = l + 4;
  *msg = m;
  return 0;
}

/*
 *
 */
static size_t
htsmsg_binary_count(htsmsg_t *msg)
{
  htsmsg_field_t *f;
  size_t len = 0;
  uint64_t u64;

  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link) {

    len += 6 + strlen(htsmsg_field_name(f));

    switch(f->hmf_type) {
    case HMF_MAP:
    case HMF_LIST:
      len += htsmsg_binary_count(f->hmf_msg);
      break;

    case HMF_STR:
      len += strlen(f->hmf_str);
      break;

    case HMF_UUID:
      len += UUID_BIN_SIZE;
      break;

    case HMF_BIN:
      len += f->hmf_binsize;
      break;

    case HMF_S64:
      u64 = f->hmf_s64;
      while(u64 != 0) {
	len++;
	u64 = u64 >> 8;
      }
      break;

    case HMF_BOOL:
      if (f->hmf_bool) len++;
      break;

    }
  }
  return len;
}

/*
 *
 */
static void
htsmsg_binary_write(htsmsg_t *msg, uint8_t *ptr)
{
  htsmsg_field_t *f;
  uint64_t u64;
  int l, i, namelen;

  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link) {
    namelen = strlen(htsmsg_field_name(f));
    *ptr++ = f->hmf_type;
    *ptr++ = namelen;

    switch(f->hmf_type) {
    case HMF_MAP:
    case HMF_LIST:
      l = htsmsg_binary_count(f->hmf_msg);
      break;

    case HMF_STR:
      l = strlen(f->hmf_str);
      break;

    case HMF_BIN:
      l = f->hmf_binsize;
      break;

    case HMF_S64:
      u64 = f->hmf_s64;
      l = 0;
      while(u64 != 0) {
	l++;
	u64 = u64 >> 8;
      }
      break;

    case HMF_BOOL:
      l = f->hmf_bool ? 1 : 0;
      break;

    case HMF_UUID:
      l = UUID_BIN_SIZE;
      break;

    default:
      abort();
    }


    *ptr++ = l >> 24;
    *ptr++ = l >> 16;
    *ptr++ = l >> 8;
    *ptr++ = l;

    if(namelen > 0) {
      memcpy(ptr, f->_hmf_name, namelen);
      ptr += namelen;
    }

    switch(f->hmf_type) {
    case HMF_MAP:
    case HMF_LIST:
      htsmsg_binary_write(f->hmf_msg, ptr);
      break;

    case HMF_STR:
      memcpy(ptr, f->hmf_str, l);
      break;

    case HMF_BIN:
      memcpy(ptr, f->hmf_bin, l);
      break;

    case HMF_S64:
      u64 = f->hmf_s64;
      for(i = 0; i < l; i++) {
	ptr[i] = u64;
	u64 = u64 >> 8;
      }
      break;

    case HMF_BOOL:
      if (f->hmf_bool)
        ptr[0] = 1;
      break;
		    
    case HMF_UUID:
      memcpy(ptr, f->hmf_uuid, UUID_BIN_SIZE);
      break;
    }
    ptr += l;
  }
}

/*
 *
 */
int
htsmsg_binary_serialize0(htsmsg_t *msg, void **datap, size_t *lenp, int maxlen)
{
  size_t len;
  uint8_t *data;

  len = htsmsg_binary_count(msg);
  if(len > maxlen)
    return -1;

  data = malloc(len);

  htsmsg_binary_write(msg, data);
  *datap = data;
  *lenp  = len;
  return 0;
}

/*
 *
 */
int
htsmsg_binary_serialize(htsmsg_t *msg, void **datap, size_t *lenp, int maxlen)
{
  size_t len;
  uint8_t *data;

  len = htsmsg_binary_count(msg);
  if(len + 4 > maxlen)
    return -1;

  data = malloc(len + 4);

  data[0] = len >> 24;
  data[1] = len >> 16;
  data[2] = len >> 8;
  data[3] = len;

  htsmsg_binary_write(msg, data + 4);
  *datap = data;
  *lenp  = len + 4;
  return 0;
}
