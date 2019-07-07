#include <mysql_priv.h>
#define
#define
#define
#define
HEADER1 "<html><head><title>"
HEADER2 "</title></head><body><table border=1>\n"
FOOTER "</table></body></html>"
FOOTER_LEN ((int)(sizeof(FOOTER)-1))
typedef struct st_html_share {
    const char *name;
    THR_LOCK lock;
    uint use_count;
    struct st_html_share *next;
} HTML_SHARE;
class ha_html: public handler
{
private:
    THR_LOCK_DATA lock;
    HTML_SHARE *share;
    FILE *fhtml;
    off_t data_start, data_end;
    off_t current_row_start, current_row_end;
public:
    ha_html(handlerton *hton, TABLE_SHARE *table_arg)
    handler(hton, table_arg) { }
    const char *table_type() const;
    const char **bas_ext() const;
    ulong index_flags(uint inx,
    uint part, bool all_parts) const;
    ulonglong table_flags() const;
    THR_LOCK_DATA **store_lock(THD *thd,
    THR_LOCK_DATA **to, enum thr_lock_type lock_type);
    int create(const char *name, TABLE *table_arg,
    HA_CREATE_INFO *create_info);
    int open(const char *name, int mode,
    uint test_if_locked);
    int close(void);
    int rnd_next(unsigned char *buf);
    int rnd_init(bool scan);
    void position(const uchar *record);
    int rnd_pos(uchar * buf, uchar *pos);
    int write_row(uchar *buf);
    int delete_row(const uchar *buf);
    int update_row(const uchar *old_data,
    uchar *new_data);
    int optimize(THD* thd, HA_CHECK_OPT* check_opt);
    int analyze(THD* thd, HA_CHECK_OPT* check_opt);
    int info(uint flag);
    int external_lock(THD *thd, int lock_type);
    int start_stmt(THD *thd, thr_lock_type lock_type);
};
void write_html(FILE *f, char *str);
int read_html(FILE *f, char *buf);
int skip_html(FILE *f, const char *pattern);    