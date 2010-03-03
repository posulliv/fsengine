/* Copyright (C) 2003 MySQL AB

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

/* pre-declare some classes */
class LineReader; 
class FormatInfo;

/** @brief
  FILESYSTEM_SHARE is a structure that will be shared among all open handlers.
  This filesystem implements the minimum of what you will probably need.
*/
typedef struct st_filesystem_share {
  char *table_name;
  FormatInfo *format_info;
  uint table_name_length,use_count;
  pthread_mutex_t mutex;
  THR_LOCK lock;
} FILESYSTEM_SHARE;

/** @brief
  Class definition for the storage engine
*/
class ha_filesystem: public handler
{
  THR_LOCK_DATA lock;      ///< MySQL lock
  FILESYSTEM_SHARE *share;    ///< Shared lock info

  LineReader *line_reader;
  int line_number;
  off_t last_offset;

public:
  ha_filesystem(handlerton *hton, TABLE_SHARE *table_arg);
  ~ha_filesystem()
  {
  }

  /** @brief
    The name that will be used for display purposes.
   */
  const char *table_type() const { return "FILESYSTEM"; }

  /** @brief
    The file extensions.
   */
  const char **bas_ext() const;

  /** @brief
    This is a list of flags that indicate what functionality the storage engine
    implements. The current table flags are documented in handler.h
  */
  ulonglong table_flags() const
  {
    return HA_REC_NOT_IN_SEQ | HA_NO_TRANSACTIONS | HA_NO_AUTO_INCREMENT;
  }

  /** @brief
    This is a bitmap of flags that indicates how the storage engine
    implements indexes. The current index flags are documented in
    handler.h. If you do not implement indexes, just return zero here.

      @details
    part is the key part to check. First key part is 0.
    If all_parts is set, MySQL wants to know the flags for the combined
    index, up to and including 'part'.
  */
  ulong index_flags(uint inx, uint part, bool all_parts) const
  {
    return 0;
  }

  /** @brief
    unireg.cc will call max_supported_record_length(), max_supported_keys(),
    max_supported_key_parts(), uint max_supported_key_length()
    to make sure that the storage engine can handle the data it is about to
    send. Return *real* limits of your storage engine here; MySQL will do
    min(your_limits, MySQL_limits) automatically.
   */
  uint max_supported_record_length() const { return HA_MAX_REC_LENGTH; }

  /** @brief
    Called in test_quick_select to determine if indexes should be used.
  */
  virtual double scan_time() { return (double) (stats.records+stats.deleted) / 20.0+10; }

  /** @brief
    This method will never be called if you do not implement indexes.
  */
  virtual double read_time(ha_rows rows) { return (double) rows /  20.0+1; }

  /*
    Everything below are methods that we implement in ha_filesystem.cc.

    Most of these methods are not obligatory, skip them and
    MySQL will treat them as not implemented
  */
  /** @brief
    We implement this in ha_filesystem.cc; it's a required method.
  */
  int open(const char *name, int mode, uint test_if_locked);    // required

  /** @brief
    We implement this in ha_filesystem.cc; it's a required method.
  */
  int close(void);                                              // required

  /** @brief
    Unlike index_init(), rnd_init() can be called two consecutive times
    without rnd_end() in between (it only makes sense if scan=1). In this
    case, the second call should prepare for the new table scan (e.g if
    rnd_init() allocates the cursor, the second call should position the
    cursor to the start of the table; no need to deallocate and allocate
    it again. This is a required method.
  */
  int rnd_init(bool scan);                                      //required
  int rnd_end();
  int rnd_next(byte *buf);                                      ///< required
  int rnd_pos(byte * buf, byte *pos);                           ///< required
  void position(const byte *record);                            ///< required
  int info(uint);                                               ///< required
  int extra(enum ha_extra_function operation);
  int external_lock(THD *thd, int lock_type);                   ///< required
  ha_rows records_in_range(uint inx, key_range *min_key,
                           key_range *max_key);
  int create(const char *name, TABLE *form,
             HA_CREATE_INFO *create_info);                      ///< required

  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type);     ///< required
};
