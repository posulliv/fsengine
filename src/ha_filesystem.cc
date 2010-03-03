
#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#define MYSQL_SERVER 1
#include "mysql_priv.h"
#include <mysql/plugin.h>

#include "ha_filesystem.h"

#include "linereader.h"
#include "formatinfo.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <map>

#if !defined(max)
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

using namespace std;

static handler *filesystem_create_handler(handlerton *hton,
					  TABLE_SHARE *table, 
					  MEM_ROOT *mem_root);

handlerton *filesystem_hton= NULL;

/* Variables for filesystem share methods */

/* 
   Hash used to track the number of open tables; variable for filesystem share
   methods
*/
static HASH filesystem_open_tables;

/* The mutex used to init the hash; variable for filesystem share methods */
pthread_mutex_t filesystem_mutex;

static byte* filesystem_get_key(FILESYSTEM_SHARE *share,uint *length,
                             my_bool not_used __attribute__((unused)))
{
  *length=share->table_name_length;
  return (byte*) share->table_name;
}

static int filesystem_init_func(void *p)
{
  DBUG_ENTER("filesystem_init_func");

  filesystem_hton= (handlerton *)p;
  VOID(pthread_mutex_init(&filesystem_mutex,MY_MUTEX_INIT_FAST));
  (void) my_hash_init(&filesystem_open_tables,system_charset_info,32,0,0,
                     (my_hash_get_key) filesystem_get_key,0,0);
  filesystem_hton->state=   SHOW_OPTION_YES;
  filesystem_hton->create=  filesystem_create_handler;
  filesystem_hton->flags=   HTON_CAN_RECREATE;

  DBUG_RETURN(0);
}


static int filesystem_done_func(void *p)
{
  int error= 0;
  DBUG_ENTER("filesystem_done_func");

  if (filesystem_open_tables.records)
    error= 1;
  my_hash_free(&filesystem_open_tables);
  pthread_mutex_destroy(&filesystem_mutex);

  DBUG_RETURN(0);
}


static FILESYSTEM_SHARE *get_share(const char *table_name, 
				   TABLE *table)
{
  FILESYSTEM_SHARE *share= NULL;
  uint length;
  char *tmp_name;

  pthread_mutex_lock(&filesystem_mutex);
  length= (uint) strlen(table_name);

  if (!(share=(FILESYSTEM_SHARE*) my_hash_search(&filesystem_open_tables,
                                           (byte*) table_name,
                                           length)))
  {
    if (! (share= (FILESYSTEM_SHARE *)
          my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                          &share, sizeof(*share),
                          &tmp_name, length+1,
                          NullS)))
    {
      pthread_mutex_unlock(&filesystem_mutex);
      return NULL;
    }

    share->use_count= 0;
    share->table_name_length= length;

    share->table_name= tmp_name;
    strmov(share->table_name,table_name);

    share->format_info= new(std::nothrow) FormatInfo();
    if (! share->format_info->Parse(table->s->connect_string.str)) 
    {
      pthread_mutex_unlock(&filesystem_mutex);
      return NULL;
    }

    if (my_hash_insert(&filesystem_open_tables, (byte*) share))
    {
      goto error;
    }
    thr_lock_init(&share->lock);
    pthread_mutex_init(&share->mutex,MY_MUTEX_INIT_FAST);
  }
  share->use_count++;
  pthread_mutex_unlock(&filesystem_mutex);

  return share;

error:
  pthread_mutex_destroy(&share->mutex);
  my_free(share, MYF(0));

  return NULL;
}


static int free_share(FILESYSTEM_SHARE *share)
{
  pthread_mutex_lock(&filesystem_mutex);
  if (! --share->use_count)
  {
    my_hash_delete(&filesystem_open_tables, (byte*) share);
    thr_lock_delete(&share->lock);
    pthread_mutex_destroy(&share->mutex);
    delete share->format_info;
    my_free(share, MYF(0));
  }
  pthread_mutex_unlock(&filesystem_mutex);

  return 0;
}

void
populate_fields(byte *buf, TABLE *table, FILESYSTEM_SHARE *share, const String &line) 
{
  int idx= 0;

  my_bitmap_map *org_bitmap= dbug_tmp_use_all_columns(table, table->write_set);

  FormatInfo *info= share->format_info;

  for (Field **field = table->field; *field; field++) 
  {
    while (idx < line.length() && info->ShouldSkip(system_charset_info, line[idx]))
    {
      ++idx;
    }

    /* out of fields?  if so, set null and continue */
    if (idx >= line.length()) 
    {
      (*field)->set_null();
      continue;
    }

    int end_idx= idx;
    while (end_idx < line.length() && !info->ShouldSkip(system_charset_info, line[end_idx]))
    {
      ++end_idx;
    }

    /* last field?  if so, rest of line goes into it*/
    if (! *(field + 1)) 
    {
      end_idx = line.length();
    }

    (*field)->store(line.ptr() + idx, end_idx - idx, system_charset_info);

    idx= end_idx + 1;
  }

  dbug_tmp_restore_column_map(table->write_set, org_bitmap);
}

static handler* filesystem_create_handler(handlerton *hton,
					  TABLE_SHARE *table, 
					  MEM_ROOT *mem_root)
{
  return new (mem_root) ha_filesystem(hton, table);
}

ha_filesystem::ha_filesystem(handlerton *hton, TABLE_SHARE *table_arg)
  : 
    line_reader(NULL), 
    line_number(0), 
    last_offset(-1), 
    handler(hton, table_arg)
{}


static const char *ha_filesystem_exts[]= 
{
  NullS
};
const char **ha_filesystem::bas_ext() const
{
  return ha_filesystem_exts;
}


int ha_filesystem::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_filesystem::open");

  if (! (share= get_share(name, table)))
  {
    DBUG_RETURN(1);
  }

  line_reader= new(std::nothrow) LineReader(share->format_info->Path());

  ref_length= sizeof(off_t);
  thr_lock_data_init(&share->lock,&lock,NULL);

  DBUG_RETURN(0);
}


int ha_filesystem::close(void)
{
  DBUG_ENTER("ha_filesystem::close");
  int rc = 0;
  delete line_reader;
  line_reader = NULL;
  DBUG_RETURN(free_share(share));
}


int ha_filesystem::rnd_init(bool )
{
  DBUG_ENTER("ha_filesystem::rnd_init");
  line_number= 0;
  DBUG_RETURN(line_reader->Open());
}


int ha_filesystem::rnd_end()
{
  DBUG_ENTER("ha_filesystem::rnd_end");
  DBUG_RETURN(0);
}


int ha_filesystem::rnd_next(byte *buf)
{
  DBUG_ENTER("ha_filesystem::rnd_next");
  if (! line_reader->Opened())
  {
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);
  }

  memset(buf, 0, table->s->null_bytes);

  if (line_reader->CurrentOffset() == line_reader->LastOffset())
  {
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  while (line_number < share->format_info->SkipLines()) 
  {
    line_number++;
    line_reader->Advance();
  }

  String line;
  line_reader->CurrentLine(&line);
  last_offset = line_reader->CurrentOffset();

  line_reader->Advance();
  populate_fields(buf, table, share, line);

  DBUG_RETURN(0);
}


void ha_filesystem::position(const byte *record)
{
  DBUG_ENTER("ha_filesystem::position");
  my_store_ptr(ref, ref_length, last_offset);
  DBUG_VOID_RETURN;
}


int ha_filesystem::rnd_pos(byte * buf, byte *pos)
{
  DBUG_ENTER("ha_filesystem::rnd_pos");
  if (! line_reader->Opened())
  {
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);
  }
  off_t offset = (off_t)my_get_ptr(pos,ref_length);
  String line;
  line_reader->LineAt(offset, &line);
  populate_fields(buf, table, share, line);
  DBUG_RETURN(0);
}


int ha_filesystem::info(uint )
{
  DBUG_ENTER("ha_filesystem::info");
  DBUG_RETURN(0);
}


int ha_filesystem::extra(enum ha_extra_function )
{
  DBUG_ENTER("ha_filesystem::extra");
  DBUG_RETURN(0);
}


ha_rows ha_filesystem::records_in_range(uint , 
                                        key_range *,
                                        key_range *)
{
  DBUG_ENTER("ha_filesystem::records_in_range");
  DBUG_RETURN(10); // low number to force index usage
}


THR_LOCK_DATA **ha_filesystem::store_lock(THD *thd,
					  THR_LOCK_DATA **to,
					  enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type=lock_type;
  *to++= &lock;
  return to;
}

int ha_filesystem::external_lock(THD *, int )
{
  DBUG_ENTER("ha_example::external_lock");
  DBUG_RETURN(0);
}


int ha_filesystem::create(const char *, 
                          TABLE *,
			  HA_CREATE_INFO *)
{
  DBUG_ENTER("ha_filesystem::create");
  DBUG_RETURN(0);
}


struct st_mysql_storage_engine filesystem_storage_engine=
{ 
  MYSQL_HANDLERTON_INTERFACE_VERSION 
};

mysql_declare_plugin(filesystem)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &filesystem_storage_engine,
  "FILESYSTEM",
  "Padraig O'Sullivan, Chip Turner",
  "Filesystem storage engine",
  PLUGIN_LICENSE_GPL,
  filesystem_init_func,                            /* Plugin Init */
  filesystem_done_func,                            /* Plugin Deinit */
  0x0001 /* 0.1 */,
  NULL,                                         /* status variables */
  NULL,                                         /* system variables */
  NULL                                          /* config options */
}
mysql_declare_plugin_end;
