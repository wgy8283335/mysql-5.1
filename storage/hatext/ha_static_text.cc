#include <mysql_priv.h>

/*Below declarations create two system variables: static_text_text,static_text_rows*/

static char *static_text_varchar;
static ulong static_text_rows;

static MYSQL_SYSVAR_STR(text,static_text_varchar,
PLUGIN_VAR_MEMALLOC,
"The value of VARCHAR columns in the static engine tables",
NULL, NULL, "Hello World!");

static MYSQL_SYSVAR_ULONG(rows, static_text_rows, 0,
"Number of rows in the static engine tables",
NULL, NULL, 3, 0, 0, 0);

static struct st_mysql_sys_var* static_text_sys_var[]={
    MYSQL_SYSVAR(text),
    MYSQL_SYSVAR(rows),
    NULL
};

/*When defining a storage engine plugin, need the following incantation*/
struct st_mysql_storage_engine static_text_storage_engine = 
{MYSQL_HANDLERTON_INTERFACE_VERSION};

mysql_declare_plugin(static_text)
{
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &static_text_storage_engine,
    "STATIC_TEXT",
    "Andrew Hutchings (Andrew.Hutchings@Sun.COM)",
    "An example static text storage engine",
    PLUGIN_LICENSE_GPL,
    static_text_init,
    NULL,
    0x0001,
    NULL,
    static_text_sys_var,
    NULL
}
mysql_declare_plugin_end;

/*The main job of the plugin initialization function is to set up the handlerton
structure.For an engine as simple as ours, we only need to provide the create() method. This 
is a handlerton method that creates a new handler object, more precisely, a new instance of
our ha_static_text class that inherits from the handler class*/
static int static_text_init(void *p)
{
    handlerton *static_text_hton = (handlerton *)p;
    static_text_hton->create = static_text_create_handler;
    return 0;
}

static handler* static_text_create_handler(handlerton *hton,
                    TABLE_SHARE *table, MEM_ROOT *mem_root)
{
    return new (mem_root) ha_static_text(hton, table);//用指定的区域去分配，从mem_root首地址开始分配
}

/*The ha_static_text class that implements interfaces defined by the handler class is the
heart of our storage engine. The handler class itself is quite big and has many virtual methods
that we can implement in ha_static_text. But luckily only a few are defined as abstract- and
it is only these methods that we are absolutely required to implement.*/
class ha_static_text: public handler
{   private:
        THR_LOCK_DATA lock;
        STATIC_SHARE *share;
        ulong returned_data;
    public:
        ha_static_text(handlerton *hton, TABLE_SHARE *table_arg):handler(hton, table_arg){}
        const char *table_type() const {return "STATIC_TEXT";}
        const char **bas_ext() const{
            static const char *exts[] = {0};
            return exts;
        }
        ulonglong table_flags() const;
        ulong index_flags(unit inx, unit part, bool all_parts) const {return 0;}
        int create(const char *name, TABLE *table_arg, HA_CREATE_INFO *create_info){return 0;}
        THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                                    enum thr_lock_type lock_type);
        int open(const char *name, int mode, unit test_if_locked);
        int close(void);
        int rnd_init(bool scan);
        int rnd_next(unsigned char *buf);
        void position(const uchar *record);
        int rnd_pos(uchar * buf, uchar *pos);
        int info(uint flag);

/*As there was no table to create in the create() method, and there is nothing to open here.
The only thing we need to do is to initialize the THR_LOCK and THR_LOCK_DATA objects. They are
used by the MySQL locking subsystem and every storage engine must provide them.
for Mysql locking code to work there should be one THR_LOCK object per table and one THR_LOCK_DATA
object per table opening.*/
        int ha_static_text::open(const char *name, int mode, uint test_if_locked)
        {
            //finds a corresponding STATIC_SHARE or creates a new one.
            //find a STATIC_SAHRE object for aspecific table.
            //name is the table name. 
            //table is the Mysql TABLE object,just like a handler, is created as many times as the table is used.
            //but Mysql TABLE has a TABLE_SHARE object which, as we can expect, is created only once
            //per table.
            //Two handler objects that refer to a same table should use the same STATIC_SHARE object,
            //otherwise they should use different STATIC_SHARE objects.
            
            share = find_or_create_share(name, table);
            if(share->use_count++ == 0)
                thr_lock_init(&share->lock);//In opening, only when the use_count is 0, the thr_lock to init
            thr_lock_data_init(&share->lock,&lock,NULL);//one open,one thr_lock_data to init
            return 0;
        }

        typedef struct st_static_text{
            const char *name;
            THR_LOCK lock;
            uint use_count;
            struct st_static_text *next;
        } STATIC_SHARE;

        static STATIC_SHARE *find_or_create_share(const char *table_name, TABLE *table)
        {
            STATIC_SHARE *share;
            //STATIC_SHARE is stored in the table->s->ha_data. As:TABLE-TABLE_SHARE-STATIC_SHARE
            //When a table is partitioned, all partitions share the same TABLE object and the same
            //TABLE_SHARE. However, partitions are different tables from the storage engine point of
            //view, and the only way to distinguish them is by a "table name", which in this case will
            //have a partition number embedded in it. To solve this problem we will link all STATIC_SHARE
            //objects for a given TABLE_SARE in a linked list and will search the list to find the
            //matching table name:
            for(share = (STATIC_SHARE*)table->s->ha_data;share;share=share->next)
                if(my_strcasecmp(table_alias_charset,table_name,share->name)==0)
                    return share;
            //if no matching STATIC_SAHRE is found in the list, we create a new one and link it in.
            share = (STATIC_SHARE*)alloc_root(&table->s->mem_root,sizeof(*share));
            bzero(share, sizeof(*share));
            share->name = strdup_root(&table->s->mem_root, table_name);
            share->next = (STATIC_SHARE*)table->s->ha_data;
            table->s->ha_data = share;
            return share;
        }
/*The close() method does the opposite of open(). It decrements the STATIC_SHARE usage counter
and destroys its THE_LOCK object if necessary. The THR_LOCK_DATA object does not need to be
destroyed*/
        int ha_static_text::close(void)
        {
            if(--share->use_count == 0)
                thr_lock_delete(&share->lock);
            return 0;
        }

/*this method returns the capabilities of the particular table. The Flag are defined in MySQL.
Specify the table is not transactional, the number of rows in the stats.records is exact, and
that the server should always call the position() method to calculate the position.*/
        ulonglong ha_static_text::table_flags() const
        {
            return HA_NO_TRANSACTIONS|HA_STATS_RECORDS_IS_EXACT|HA_REC_NOT_IN_SEQ;
        }
/*this method stores the lock of the level lock_type.We will use Mysql table-level locking.
The function store_lock() is identical in all table-level locking engines.*/
        THR_LOCK_DATA **ha_static_text::store_lock(THD *thd, THR_LOCK_DATA **to,
                                                    enum thr_lock_type lock_type)
        {
            if(lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
                lock.type = lock_type;
            *to ++= &lock;
            return to;
        }
/*Fields, represented as objects of the Field class, and available via the TABLE object,
know how to store and convert themselves. We have seen how to use the Field classes in
the information Schema chapters; storage engines use them similarly. This is how one can
use them to fill in a row buffer*/
        static void fill_record(TABLE *table, unsigned char *buf, ulong row_num)
        {
            my_bitmap_map *old_map = dbug_tmp_use_all_columns(table, table->write_set);
            for(unit i=0;i<table->s->fields:i++)
            {
                Field *field = table->field[i];
                my_ptrdiff_t offset;
                offset = (my_ptrdiff_t)(buf-table->record[0]);
                field->move_field_offset(offset);
                field->set_notnull();
                if(field->type() == MYSQL_TYPE_VARCHAR)
                    field->store(static_text_varchar, strlen(static_text_varchar),system_charset_info);
                else if(field->type() == MYSQL_TYPE_LONG)
                    field->store(row_num,false);
                field->move_field_offset(-offset);
            }
            dbug_tmp_restore_column_map(table->write_set, old_map);
        }

        int ha_static_text::rnd_init(bool scan)
        {
            returned_data=0;
            return 0;
        }

        int ha_static_text::rnd_next(unsigned char *buf)
        {
            if(returned_data>=static_text_rows)
                return HA_ERR_END_OF_FILE;
            fill_record(table,buf,++returned_data);
            return 0;
        }
/*This method remembers a current record position in the internal handler variable.
A position is whatever the engine uses to uniquely
identify the record; for example, a primary key value, a row ID, an
address in memory, or an offset in the data file.*/
        void ha_static_text::position(const uchar *record)
        {
            *(ulong*)ref = returned_data;
        }
/*The opposite of position()-this method tabkes a position as an argument and returns a record
identified by it.*/
        int ha_static_text::rnd_pos(uchar *buf, uchar *pos)
        {
            ulong row_num;
            memcpy(&row_num, pos, sizeof(row_num));
            fill_record(table,buf,row_num);
            return 0;
        }

        int ha_static_text::info(uint flag)
        {
            if(flag&HA_STATUS_VARIABLE)
                stats.records=static_text_rows;
            return 0;
        }
}