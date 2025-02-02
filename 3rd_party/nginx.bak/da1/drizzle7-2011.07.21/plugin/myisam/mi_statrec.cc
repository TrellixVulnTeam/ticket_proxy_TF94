/* Copyright (C) 2000-2002, 2004-2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

	/* Functions to handle fixed-length-records */

#include "myisam_priv.h"

using namespace drizzled;

int _mi_write_static_record(MI_INFO *info, const unsigned char *record)
{
  unsigned char temp[8];				/* max pointer length */
  if (info->s->state.dellink != HA_OFFSET_ERROR &&
      !info->append_insert_at_end)
  {
    internal::my_off_t filepos=info->s->state.dellink;
    info->rec_cache.seek_not_done=1;		/* We have done a seek */
    if (info->s->file_read(info, &temp[0],info->s->base.rec_reflength,
		info->s->state.dellink+1,
		 MYF(MY_NABP)))
      goto err;
    info->s->state.dellink= _mi_rec_pos(info->s,temp);
    info->state->del--;
    info->state->empty-=info->s->base.pack_reclength;
    if (info->s->file_write(info, record, info->s->base.reclength,
		  filepos,
		  MYF(MY_NABP)))
      goto err;
  }
  else
  {
    if (info->state->data_file_length > info->s->base.max_data_file_length-
	info->s->base.pack_reclength)
    {
      errno=HA_ERR_RECORD_FILE_FULL;
      return(2);
    }
    if (info->opt_flag & WRITE_CACHE_USED)
    {				/* Cash in use */
      if (my_b_write(&info->rec_cache, record,
		     info->s->base.reclength))
	goto err;
      if (info->s->base.pack_reclength != info->s->base.reclength)
      {
	uint32_t length=info->s->base.pack_reclength - info->s->base.reclength;
	memset(temp, 0, length);
	if (my_b_write(&info->rec_cache, temp,length))
	  goto err;
      }
    }
    else
    {
      info->rec_cache.seek_not_done=1;		/* We have done a seek */
      if (info->s->file_write(info, record, info->s->base.reclength,
		    info->state->data_file_length,
		    info->s->write_flag))
        goto err;
      if (info->s->base.pack_reclength != info->s->base.reclength)
      {
	uint32_t length=info->s->base.pack_reclength - info->s->base.reclength;
	memset(temp, 0, length);
	if (info->s->file_write(info, temp,length,
		      info->state->data_file_length+
		      info->s->base.reclength,
		      info->s->write_flag))
    goto err;
      }
    }
    info->state->data_file_length+=info->s->base.pack_reclength;
    info->s->state.split++;
  }
  return 0;
 err:
  return 1;
}

int _mi_update_static_record(MI_INFO *info, internal::my_off_t pos, const unsigned char *record)
{
  info->rec_cache.seek_not_done=1;		/* We have done a seek */
  return (info->s->file_write(info,
                              record, info->s->base.reclength,
                              pos,
                              MYF(MY_NABP)) != 0);
}


int _mi_delete_static_record(MI_INFO *info)
{
  unsigned char temp[9];				/* 1+sizeof(uint32) */

  info->state->del++;
  info->state->empty+=info->s->base.pack_reclength;
  temp[0]= '\0';			/* Mark that record is deleted */
  _mi_dpointer(info,temp+1,info->s->state.dellink);
  info->s->state.dellink = info->lastpos;
  info->rec_cache.seek_not_done=1;
  return (info->s->file_write(info,(unsigned char*) temp, 1+info->s->rec_reflength,
		    info->lastpos, MYF(MY_NABP)) != 0);
}


int _mi_cmp_static_record(register MI_INFO *info, register const unsigned char *old)
{
  if (info->opt_flag & WRITE_CACHE_USED)
  {
    if (flush_io_cache(&info->rec_cache))
    {
      return(-1);
    }
    info->rec_cache.seek_not_done=1;		/* We have done a seek */
  }

  if ((info->opt_flag & READ_CHECK_USED))
  {						/* If check isn't disabled  */
    info->rec_cache.seek_not_done=1;		/* We have done a seek */
    if (info->s->file_read(info, info->rec_buff, info->s->base.reclength,
		 info->lastpos,
		 MYF(MY_NABP)))
      return(-1);
    if (memcmp(info->rec_buff, old,
	       (uint) info->s->base.reclength))
    {
      errno=HA_ERR_RECORD_CHANGED;		/* Record have changed */
      return(1);
    }
  }
  return(0);
}


int _mi_cmp_static_unique(MI_INFO *info, MI_UNIQUEDEF *def,
			  const unsigned char *record, internal::my_off_t pos)
{
  info->rec_cache.seek_not_done=1;		/* We have done a seek */
  if (info->s->file_read(info, info->rec_buff, info->s->base.reclength,
	       pos, MYF(MY_NABP)))
    return(-1);
  return(mi_unique_comp(def, record, info->rec_buff,
			     def->null_are_equal));
}


	/* Read a fixed-length-record */
	/* Returns 0 if Ok. */
	/*	   1 if record is deleted */
	/*	  MY_FILE_ERROR on read-error or locking-error */

int _mi_read_static_record(register MI_INFO *info, register internal::my_off_t pos,
			   register unsigned char *record)
{
  int error;

  if (pos != HA_OFFSET_ERROR)
  {
    if (info->opt_flag & WRITE_CACHE_USED &&
	info->rec_cache.pos_in_file <= pos &&
	flush_io_cache(&info->rec_cache))
      return(-1);
    info->rec_cache.seek_not_done=1;		/* We have done a seek */

    error=info->s->file_read(info, record, info->s->base.reclength,
		   pos,MYF(MY_NABP)) != 0;
    fast_mi_writeinfo(info);
    if (! error)
    {
      if (!*record)
      {
	errno=HA_ERR_RECORD_DELETED;
	return(1);				/* Record is deleted */
      }
      info->update|= HA_STATE_AKTIV;		/* Record is read */
      return(0);
    }
    return(-1);					/* Error on read */
  }
  fast_mi_writeinfo(info);			/* No such record */
  return(-1);
}



int _mi_read_rnd_static_record(MI_INFO *info, unsigned char *buf,
			       register internal::my_off_t filepos,
			       bool skip_deleted_blocks)
{
  int locked,error,cache_read;
  MYISAM_SHARE *share=info->s;

  cache_read=0;
  if (info->opt_flag & WRITE_CACHE_USED &&
      (info->rec_cache.pos_in_file <= filepos || skip_deleted_blocks) &&
      flush_io_cache(&info->rec_cache))
    return(errno);
  if (info->opt_flag & READ_CACHE_USED)
  {						/* Cache in use */
    if (filepos == my_b_tell(&info->rec_cache) &&
	(skip_deleted_blocks || !filepos))
    {
      cache_read=1;				/* Read record using cache */
    }
    else
      info->rec_cache.seek_not_done=1;		/* Filepos is changed */
  }
  locked=0;
  if (info->lock_type == F_UNLCK)
  {
    if (filepos >= info->state->data_file_length)
    {						/* Test if new records */
      if (_mi_readinfo(info,F_RDLCK,0))
	return(errno);
      locked=1;
    }
    else
    {						/* We don't nead new info */
#ifndef UNSAFE_LOCKING
      locked=1;
#else
      info->tmp_lock_type=F_RDLCK;
#endif
    }
  }
  if (filepos >= info->state->data_file_length)
  {
    fast_mi_writeinfo(info);
    return(errno=HA_ERR_END_OF_FILE);
  }
  info->lastpos= filepos;
  info->nextpos= filepos+share->base.pack_reclength;

  if (! cache_read)			/* No cacheing */
  {
    if ((error=_mi_read_static_record(info,filepos,buf)))
    {
      if (error > 0)
	error=errno=HA_ERR_RECORD_DELETED;
      else
	error=errno;
    }
    return(error);
  }

	/* Read record with cacheing */
  error=my_b_read(&info->rec_cache,(unsigned char*) buf,share->base.reclength);
  if (info->s->base.pack_reclength != info->s->base.reclength && !error)
  {
    char tmp[8];				/* Skill fill bytes */
    error=my_b_read(&info->rec_cache,(unsigned char*) tmp,
		    info->s->base.pack_reclength - info->s->base.reclength);
  }
  if (locked)
    _mi_writeinfo(info,0);		/* Unlock keyfile */
  if (!error)
  {
    if (!buf[0])
    {						/* Record is removed */
      return(errno=HA_ERR_RECORD_DELETED);
    }
						/* Found and may be updated */
    info->update|= HA_STATE_AKTIV | HA_STATE_KEY_CHANGED;
    return(0);
  }
  /* errno should be set if rec_cache.error == -1 */
  if (info->rec_cache.error != -1 || errno == 0)
    errno=HA_ERR_WRONG_IN_RECORD;
  return(errno);			/* Something wrong (EOF?) */
}
