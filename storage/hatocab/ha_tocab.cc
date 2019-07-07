#include "ha_tocab.h"

static handler* tocab_create_handler(handlerton *hton,TABLE_SHARE *table, MEM_ROOT *mem_root)
{
    return new (mem_root) ha_tocab(hton, table);
}
static int tocab_init(void *p)
{
    handlerton *tocab_hton = (handlerton *)p;
    tocab_hton->create = tocab_create_handler;
    return 0;
}
struct st_mysql_storage_engine tocab_storage_engine = {MYSQL_HANDLERTON_INTERFACE_VERSION };
mysql_declare_plugin(tocab)
{
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &tocab_storage_engine,
    "TOCAB",
    "Sergei Golubchik",
    "An example storage engine that uses Tokyo Cabinet library",
    PLUGIN_LICENSE_GPL,
    tocab_init,
    NULL,
    0x0001,
    NULL,
    NULL,
    NULL
}
mysql_declare_plugin_end;

static TOCAB_SHARE *find_or_create_share(const char *table_name, TABLE *table)
{
    TOCAB_SHARE *share;
    for (share = (TOCAB_SHARE*)table->s->ha_data;share; share = share->next)
        if (my_strcasecmp(table_alias_charset,table_name, share->name) == 0)
            return share;
    share = (TOCAB_SHARE*)alloc_root(&table->s->mem_root,
    sizeof(*share));
    bzero(share, sizeof(*share));
    share->name = strdup_root(&table->s->mem_root, table_name);
    share->next = (TOCAB_SHARE*)table->s->ha_data;
    table->s->ha_data = share;
    return share;
}

THR_LOCK_DATA **ha_tocab::store_lock(THD *thd,THR_LOCK_DATA **to, enum thr_lock_type lock_type)
{
if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type = lock_type;
*to ++= &lock;
return to;
}

const char *ha_tocab::table_type() const { return "TOCAB"; }
const char **ha_tocab::bas_ext() const {
    static const char *exts[] = { ".tocab", 0 };
    return exts;
}

const char *ha_tocab::index_type(uint key_number)
{
    return "BTREE";
}
uint ha_tocab::max_supported_keys() const { return 128; }

ulong ha_tocab::index_flags(uint inx, uint part,bool all_parts) const
{
    return HA_READ_NEXT | HA_READ_PREV | HA_READ_ORDER | HA_READ_RANGE | (inx ? HA_KEYREAD_ONLY : 0);
}

ulonglong ha_tocab::table_flags() const
{
    return HA_NO_TRANSACTIONS |
    HA_TABLE_SCAN_ON_INDEX |
    HA_REC_NOT_IN_SEQ |
    HA_NULL_IN_KEY |
    HA_STATS_RECORDS_IS_EXACT |
    HA_CAN_INDEX_BLOBS |
    HA_AUTO_PART_KEY |
    HA_PRIMARY_KEY_IN_READ_INDEX |
    HA_REQUIRE_PRIMARY_KEY ;
}