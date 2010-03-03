
#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

/* forward declarations */
class LineReader; 
class FormatInfo;

typedef struct st_filesystem_share 
{
  char *table_name;
  FormatInfo *format_info;
  uint table_name_length,use_count;
  pthread_mutex_t mutex;
  THR_LOCK lock;
} FILESYSTEM_SHARE;


class ha_filesystem: public handler
{
  THR_LOCK_DATA lock;
  FILESYSTEM_SHARE *share;

  LineReader *line_reader;
  int line_number;
  off_t last_offset;

public:

  ha_filesystem(handlerton *hton, TABLE_SHARE *table_arg);
  ~ha_filesystem() {}

  const char *table_type() const 
  { 
    return "FILESYSTEM"; 
  }

  const char **bas_ext() const;

  ulonglong table_flags() const
  {
    return HA_REC_NOT_IN_SEQ | HA_NO_TRANSACTIONS | HA_NO_AUTO_INCREMENT;
  }

  ulong index_flags(uint inx, uint part, bool all_parts) const
  {
    return 0;
  }

  uint max_supported_record_length() const { return HA_MAX_REC_LENGTH; }

  virtual double scan_time() 
  { 
    return (double) (stats.records + stats.deleted) / 20.0 + 10; 
  }

  virtual double read_time(ha_rows rows) 
  { 
    return (double) rows / 20.0 + 1; 
  }

  int open(const char *name, int mode, uint test_if_locked);

  int close(void);

  int rnd_init(bool scan);
  int rnd_end();
  int rnd_next(byte *buf);
  int rnd_pos(byte * buf, byte *pos);
  void position(const byte *record);
  int info(uint);
  int extra(enum ha_extra_function operation);
  int external_lock(THD *thd, int lock_type);
  ha_rows records_in_range(uint inx, key_range *min_key,
                           key_range *max_key);
  int create(const char *name, TABLE *form,
             HA_CREATE_INFO *create_info);

  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type);
};
