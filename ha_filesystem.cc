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

/**
  @file ha_filesystem.cc

  @brief
  The ha_filesystem engine is a stubbed storage engine for filesystem purposes only;
  it does nothing at this point. Its purpose is to provide a source
  code illustration of how to begin writing new storage engines; see also
  /storage/filesystem/ha_filesystem.h.

  @details
  ha_filesystem will let you create/open/delete tables, but
  nothing further (for filesystem, indexes are not supported nor can data
  be stored in the table). Use this filesystem as a template for
  implementing the same functionality in your own storage engine. You
  can enable the filesystem storage engine in your build by doing the
  following during your build process:<br> ./configure
  --with-filesystem-storage-engine

  Once this is done, MySQL will let you create tables with:<br>
  CREATE TABLE <table name> (...) ENGINE=FILESYSTEM;

  The filesystem storage engine is set up to use table locks. It
  implements an filesystem "SHARE" that is inserted into a hash by table
  name. You can use this to store information of state that any
  filesystem handler object will be able to see when it is using that
  table.

  Please read the object definition in ha_filesystem.h before reading the rest
  of this file.

  @note
  When you create an FILESYSTEM table, the MySQL Server creates a table .frm
  (format) file in the database directory, using the table name as the file
  name as is customary with MySQL. No other files are created. To get an idea
  of what occurs, here is an filesystem select that would do a scan of an entire
  table:

  @code
  ha_filesystem::store_lock
  ha_filesystem::external_lock
  ha_filesystem::info
  ha_filesystem::rnd_init
  ha_filesystem::extra
  ENUM HA_EXTRA_CACHE        Cache record in HA_rrnd()
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::rnd_next
  ha_filesystem::extra
  ENUM HA_EXTRA_NO_CACHE     End caching of records (def)
  ha_filesystem::external_lock
  ha_filesystem::extra
  ENUM HA_EXTRA_RESET        Reset database to after open
  @endcode

  Here you see that the filesystem storage engine has 9 rows called before
  rnd_next signals that it has reached the end of its data. Also note that
  the table in question was already opened; had it not been open, a call to
  ha_filesystem::open() would also have been necessary. Calls to
  ha_filesystem::extra() are hints as to what will be occuring to the request.

  Happy coding!<br>
    -Brian
*/

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#define MYSQL_SERVER 1
#include "mysql_priv.h"
#include "ha_filesystem.h"
#include <mysql/plugin.h>

#include "linereader.h"
#include "formatinfo.h"

static handler *filesystem_create_handler(handlerton *hton,
					  TABLE_SHARE *table, 
					  MEM_ROOT *mem_root);

handlerton *filesystem_hton;

/* Variables for filesystem share methods */

/* 
   Hash used to track the number of open tables; variable for filesystem share
   methods
*/
static HASH filesystem_open_tables;

/* The mutex used to init the hash; variable for filesystem share methods */
pthread_mutex_t filesystem_mutex;

/**
  @brief
  Function we use in the creation of our hash to get key.
*/

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
  (void) hash_init(&filesystem_open_tables,system_charset_info,32,0,0,
                   (hash_get_key) filesystem_get_key,0,0);

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
  hash_free(&filesystem_open_tables);
  pthread_mutex_destroy(&filesystem_mutex);

  DBUG_RETURN(0);
}


/**
  @brief
  Filesystem of simple lock controls. The "share" it creates is a
  structure we will pass to each filesystem handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/

static FILESYSTEM_SHARE *get_share(const char *table_name, 
				   TABLE *table)
{
  FILESYSTEM_SHARE *share;
  uint length;
  char *tmp_name;

  pthread_mutex_lock(&filesystem_mutex);
  length=(uint) strlen(table_name);

  if (!(share=(FILESYSTEM_SHARE*) hash_search(&filesystem_open_tables,
                                           (byte*) table_name,
                                           length)))
  {
    if (!(share=(FILESYSTEM_SHARE *)
          my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                          &share, sizeof(*share),
                          &tmp_name, length+1,
                          NullS)))
    {
      pthread_mutex_unlock(&filesystem_mutex);
      return NULL;
    }

    share->use_count=0;
    share->table_name_length=length;

    share->table_name=tmp_name;
    strmov(share->table_name,table_name);

    share->format_info = new FormatInfo();
    if (!share->format_info->Parse(table->s->connect_string.str)) {
      pthread_mutex_unlock(&filesystem_mutex);
      return NULL;
    }

    if (my_hash_insert(&filesystem_open_tables, (byte*) share))
      goto error;
    thr_lock_init(&share->lock);
    pthread_mutex_init(&share->mutex,MY_MUTEX_INIT_FAST);
  }
  share->use_count++;
  pthread_mutex_unlock(&filesystem_mutex);

  return share;

error:
  pthread_mutex_destroy(&share->mutex);
  my_free((gptr) share, MYF(0));

  return NULL;
}


/**
  @brief
  Free lock controls. We call this whenever we close a table. If the table had
  the last reference to the share, then we free memory associated with it.
*/

static int free_share(FILESYSTEM_SHARE *share)
{
  pthread_mutex_lock(&filesystem_mutex);
  if (!--share->use_count)
  {
    hash_delete(&filesystem_open_tables, (byte*) share);
    thr_lock_delete(&share->lock);
    pthread_mutex_destroy(&share->mutex);
    delete share->format_info;
    my_free((gptr) share, MYF(0));
  }
  pthread_mutex_unlock(&filesystem_mutex);

  return 0;
}

void
populate_fields(byte *buf, TABLE *table, FILESYSTEM_SHARE *share, const String &line) {
  int idx = 0;

  my_bitmap_map *org_bitmap= dbug_tmp_use_all_columns(table, table->write_set);

  FormatInfo *info = share->format_info;

  for (Field **field = table->field; *field; field++) {
    while (idx < line.length() && info->ShouldSkip(system_charset_info, line[idx]))
      ++idx;

    /* out of fields?  if so, set null and continue */
    if (idx >= line.length()) {
      (*field)->set_null();
      continue;
    }

    int end_idx = idx;
    while (end_idx < line.length() && !info->ShouldSkip(system_charset_info, line[end_idx]))
      ++end_idx;

    /* last field?  if so, rest of line goes into it*/
    if (!*(field + 1)) {
      end_idx = line.length();
    }

    (*field)->store(line.ptr() + idx, end_idx - idx, system_charset_info);

    idx = end_idx + 1;
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
  : line_reader(NULL), line_number(0), last_offset(-1), handler(hton, table_arg)
{}


/**
  @brief
  If frm_error() is called then we will use this to determine
  the file extensions that exist for the storage engine. This is also
  used by the default rename_table and delete_table method in
  handler.cc.

  @see
  rename_table method in handler.cc and
  delete_table method in handler.cc
*/

static const char *ha_filesystem_exts[] = {
  NullS
};
const char **ha_filesystem::bas_ext() const
{
  return ha_filesystem_exts;
}


/**
  @brief
  Used for opening tables. The name will be the name of the file.

  @details
  A table is opened when it needs to be opened; e.g. when a request comes in
  for a SELECT on the table (tables are not open and closed for each request,
  they are cached).

  Called from handler.cc by handler::ha_open(). The server opens all tables by
  calling ha_open() which then calls the handler specific open().

  @see
  handler::ha_open() in handler.cc
*/

int ha_filesystem::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_filesystem::open");

  if (!(share = get_share(name, table)))
    DBUG_RETURN(1);

  line_reader = new LineReader(share->format_info->Path());

  ref_length = sizeof(off_t);
  thr_lock_data_init(&share->lock,&lock,NULL);

  DBUG_RETURN(0);
}


/**
  @brief
  Closes a table. We call the free_share() function to free any resources
  that we have allocated in the "shared" structure.

  @details
  Called from sql_base.cc, sql_select.cc, and table.cc. In sql_select.cc it is
  only used to close up temporary tables or during the process where a
  temporary table is converted over to being a myisam table.

  For sql_base.cc look at close_data_tables().

  @see
  sql_base.cc, sql_select.cc and table.cc
*/

int ha_filesystem::close(void)
{
  DBUG_ENTER("ha_filesystem::close");
  int rc = 0;
  delete line_reader;
  line_reader = NULL;
  DBUG_RETURN(free_share(share));
}


/**
  @brief
  rnd_init() is called when the system wants the storage engine to do a table
  scan. See the filesystem in the introduction at the top of this file to see when
  rnd_init() is called.

    @details
  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc,
  and sql_update.cc.

    @see
  filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc and sql_update.cc
*/
int ha_filesystem::rnd_init(bool scan)
{
  DBUG_ENTER("ha_filesystem::rnd_init");
  line_number = 0;
  DBUG_RETURN(line_reader->Open());
}

int ha_filesystem::rnd_end()
{
  DBUG_ENTER("ha_filesystem::rnd_end");
  DBUG_RETURN(0);
}


/**
  @brief
  This is called for each row of the table scan. When you run out of records
  you should return HA_ERR_END_OF_FILE. Fill buff up with the row information.
  The Field structure for the table is the key to getting data into buf
  in a manner that will allow the server to understand it.

    @details
  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc,
  and sql_update.cc.

    @see
  filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc and sql_update.cc
*/
int ha_filesystem::rnd_next(byte *buf)
{
  DBUG_ENTER("ha_filesystem::rnd_next");
  if (!line_reader->Opened())
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);

  memset(buf, 0, table->s->null_bytes);

  if (line_reader->CurrentOffset() == line_reader->LastOffset())
    DBUG_RETURN(HA_ERR_END_OF_FILE);

  while (line_number < share->format_info->SkipLines()) {
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


/**
  @brief
  position() is called after each call to rnd_next() if the data needs
  to be ordered. You can do something like the following to store
  the position:
    @code
  my_store_ptr(ref, ref_length, current_position);
    @endcode

    @details
  The server uses ref to store data. ref_length in the above case is
  the size needed to store current_position. ref is just a byte array
  that the server will maintain. If you are using offsets to mark rows, then
  current_position should be the offset. If it is a primary key like in
  BDB, then it needs to be a primary key.

  Called from filesort.cc, sql_select.cc, sql_delete.cc, and sql_update.cc.

    @see
  filesort.cc, sql_select.cc, sql_delete.cc and sql_update.cc
*/
void ha_filesystem::position(const byte *record)
{
  DBUG_ENTER("ha_filesystem::position");
  my_store_ptr(ref, ref_length, last_offset);
  DBUG_VOID_RETURN;
}


/**
  @brief
  This is like rnd_next, but you are given a position to use
  to determine the row. The position will be of the type that you stored in
  ref. You can use ha_get_ptr(pos,ref_length) to retrieve whatever key
  or position you saved when position() was called.

    @details
  Called from filesort.cc, records.cc, sql_insert.cc, sql_select.cc, and sql_update.cc.

    @see
  filesort.cc, records.cc, sql_insert.cc, sql_select.cc and sql_update.cc
*/
int ha_filesystem::rnd_pos(byte * buf, byte *pos)
{
  DBUG_ENTER("ha_filesystem::rnd_pos");
  if (!line_reader->Opened())
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);
  off_t offset = (off_t)my_get_ptr(pos,ref_length);
  String line;
  line_reader->LineAt(offset, &line);
  populate_fields(buf, table, share, line);
  DBUG_RETURN(0);
}


/**
  @brief
  ::info() is used to return information to the optimizer. See my_base.h for
  the complete description.

    @details
  Currently this table handler doesn't implement most of the fields really needed.
  SHOW also makes use of this data.

  You will probably want to have the following in your code:
    @code
  if (records < 2)
    records = 2;
    @endcode
  The reason is that the server will optimize for cases of only a single
  record. If, in a table scan, you don't know the number of records, it
  will probably be better to set records to two so you can return as many
  records as you need. Along with records, a few more variables you may wish
  to set are:
    records
    deleted
    data_file_length
    index_file_length
    delete_length
    check_time
  Take a look at the public variables in handler.h for more information.

  Called in filesort.cc, ha_heap.cc, item_sum.cc, opt_sum.cc, sql_delete.cc,
  sql_delete.cc, sql_derived.cc, sql_select.cc, sql_select.cc, sql_select.cc,
  sql_select.cc, sql_select.cc, sql_show.cc, sql_show.cc, sql_show.cc, sql_show.cc,
  sql_table.cc, sql_union.cc, and sql_update.cc.

    @see
  filesort.cc, ha_heap.cc, item_sum.cc, opt_sum.cc, sql_delete.cc, sql_delete.cc,
  sql_derived.cc, sql_select.cc, sql_select.cc, sql_select.cc, sql_select.cc,
  sql_select.cc, sql_show.cc, sql_show.cc, sql_show.cc, sql_show.cc, sql_table.cc,
  sql_union.cc and sql_update.cc
*/
int ha_filesystem::info(uint flag)
{
  DBUG_ENTER("ha_filesystem::info");
  DBUG_RETURN(0);
}


/**
  @brief
  extra() is called whenever the server wishes to send a hint to
  the storage engine. The myisam engine implements the most hints.
  ha_innodb.cc has the most exhaustive list of these hints.

    @see
  ha_innodb.cc
*/
int ha_filesystem::extra(enum ha_extra_function operation)
{
  DBUG_ENTER("ha_filesystem::extra");
  DBUG_RETURN(0);
}


/**
  @brief
  Given a starting key and an ending key, estimate the number of rows that
  will exist between the two keys.

  @details
  end_key may be empty, in which case determine if start_key matches any rows.

  Called from opt_range.cc by check_quick_keys().

  @see
  check_quick_keys() in opt_range.cc
*/
ha_rows ha_filesystem::records_in_range(uint inx, key_range *min_key,
                                     key_range *max_key)
{
  DBUG_ENTER("ha_filesystem::records_in_range");
  DBUG_RETURN(10);                         // low number to force index usage
}

// TODO: figure this out
THR_LOCK_DATA **ha_filesystem::store_lock(THD *thd,
					  THR_LOCK_DATA **to,
					  enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type=lock_type;
  *to++= &lock;
  return to;
}

int ha_filesystem::external_lock(THD *thd, int lock_type)
{
  DBUG_ENTER("ha_example::external_lock");
  DBUG_RETURN(0);
}

/**
  @brief
  create() is called to create a database. The variable name will have the name
  of the table.

  @details
  When create() is called you do not need to worry about
  opening the table. Also, the .frm file will have already been
  created so adjusting create_info is not necessary. You can overwrite
  the .frm file at this point if you wish to change the table
  definition, but there are no methods currently provided for doing
  so.

  Called from handle.cc by ha_create_table().

  @see
  ha_create_table() in handle.cc
*/

int ha_filesystem::create(const char *name, TABLE *table_arg,
			  HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_filesystem::create");
  DBUG_RETURN(0);
}


struct st_mysql_storage_engine filesystem_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

mysql_declare_plugin(filesystem)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &filesystem_storage_engine,
  "FILESYSTEM",
  "Chip Turner",
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
