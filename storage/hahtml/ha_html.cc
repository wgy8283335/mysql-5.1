#include "ha_html.h"

static handler* html_create_handler(handlerton *hton,TABLE_SHARE *table, MEM_ROOT *mem_root)
{
    return new (mem_root) ha_html(hton, table);
}

static int html_init(void *p)
{
    handlerton *html_hton = (handlerton *)p;
    html_hton->create = html_create_handler;
    return 0;
}

struct st_mysql_storage_engine html_storage_engine =
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

mysql_declare_plugin(html)
{
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &html_storage_engine,
    "HTML",
    "Sergei Golubchik",
    "An example HTML storage engine",
    PLUGIN_LICENSE_GPL,
    html_init,
    NULL,
    0x0001,
    NULL,
    NULL,
    NULL
}
mysql_declare_plugin_end;

const char *ha_html::table_type() const
{
    return "HTML";
}

const char **ha_html::bas_ext() const
{
    static const char *exts[] = { ".html", 0 };
    return exts;
}

ulong ha_html::index_flags(uint inx, uint part, bool all_parts) const
{
    return 0;
}

ulonglong ha_html::table_flags() const
{
    return HA_NO_TRANSACTIONS | HA_REC_NOT_IN_SEQ | HA_NO_BLOBS;
}

THR_LOCK_DATA **ha_html::store_lock(THD *thd,THR_LOCK_DATA **to, enum thr_lock_type lock_type)
{
    if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type = lock_type;
    *to ++= &lock;
    return to;
}

static HTML_SHARE *find_or_create_share(const char *table_name, TABLE *table)
{
    HTML_SHARE *share;
    for (share = (HTML_SHARE*)table->s->ha_data;share; share = share->next)
        if (my_strcasecmp(table_alias_charset,table_name, share->name) == 0)
            return share;
    share = (HTML_SHARE*)alloc_root(&table->s->mem_root,
    sizeof(*share));
    bzero(share, sizeof(*share));
    share->name = strdup_root(&table->s->mem_root, table_name);
    share->next = (HTML_SHARE*)table->s->ha_data;
    table->s->ha_data = share;
    return share;
}

int ha_html::create(const char *name, TABLE *table_arg,HA_CREATE_INFO *create_info)
{
    char buf[FN_REFLEN+10];
    strcpy(buf, name);
    strcat(buf, *bas_ext())
    FILE *f = fopen(buf, "w");
    if (f == 0)
        return errno;
    fprintf(f, HEADER1);
    write_html(f, table_arg->s->table_name.str);
    fprintf(f, HEADER2 "<tr>");
    for (uint i = 0; i < table_arg->s->fields; i++) {
        fprintf(f, "<th>");
        write_html(f, table_arg->field[i]->field_name);
        fprintf(f, "</th>");
    }
    fprintf(f, "</tr>");
    fprintf(f, FOOTER);
    fclose(f);
    return 0;
}
/*
Opening it is easy too. We generate the filename and open the file just as in the
create() method. The only difference is that we need to remember the FILE pointer
to be able to read the data later, and we store it in fhtml , which has to be a member
of the ha_html object.
When parsing an HTML file we will often need to skip over known patterns in the
text. Instead of using a special library or a custom pattern parser for that, let's try
to use scanf() —it exists everywhere, has a built-in pattern matching language,
and it is powerful enough for our purposes. For convenience, we will wrap it in a
skip_html() function that takes a scanf() format and returns the number of bytes
skipped.
*/
int ha_html::open(const char *name, int mode, uint test_if_locked)
{
    char buf[FN_REFLEN+10];
    strcpy(buf, name);
    strcat(buf, *bas_ext());
    fhtml = fopen(buf, "r+");
    if (fhtml == 0)
        return errno;
    skip_html(fhtml, HEADER1 "%*[^<]" HEADER2 "<tr>");
    for (uint i = 0; i < table->s->fields; i++) {
        skip_html(fhtml, "<th>%*[^<]</th>");
    }
    skip_html(fhtml, "</tr>");
    data_start = ftell(fhtml);
    share = find_or_create_share(name, table);
    if (share->use_count++ == 0)
        thr_lock_init(&share->lock);
    thr_lock_data_init(&share->lock,&lock,NULL);
    return 0;
}   

int ha_html::close(void)
{
fclose(fhtml);
if (--share->use_count == 0)
thr_lock_delete(&share->lock);
return 0;
}
/*reading data*/
int ha_html::rnd_next(unsigned char *buf)
{
    fseek(fhtml, current_row_end, SEEK_SET);//移动指针到当前行尾
    for (;;) //开始寻找row。
    {
        current_row_start= ftell(fhtml);
        if (current_row_start >= data_end)//如果当前行指针大于数据行尾，则返回错误
            return HA_ERR_END_OF_FILE;
        if (skip_html(fhtml, "<tr>") == 4)//如果发现<tr>则找到了行
            break;
        if (skip_html(fhtml, "!--%*[^-]-->\n") >=7)//如果匹配到这个，说明是被删除的行，则需要继续寻找。
            continue;
        return HA_ERR_CRASHED;//否则返回异常
    }
    my_bitmap_map *old_map = dbug_tmp_use_all_columns(table, table->write_set);
    my_ptrdiff_t offset = (my_ptrdiff_t)(buf-table->record[0]);
    for (uint i = 0; i < table->s->fields; i++) {
        Field *field = table->field[i];
        field->move_field_offset(offset);
        if (skip_html(fhtml, "<%*[td/]>") == 5)//如果读到</td>，代表到了列尾。所以值设置为空。
            field->set_null();
        else {//没有读到</td>则代表有值。
            char *buf = (char*)
            malloc(field->max_display_length() + 1);
            int buf_len = read_html(fhtml, buf);
            field->set_notnull();
            field->store(buf, buf_len, &my_charset_bin);
            skip_html(fhtml, "</td>");
            free(buf);
        }
        field->move_field_offset(-offset);
    }
    skip_html(fhtml, "</tr>\n");
    dbug_tmp_restore_column_map(table->write_set, old_map);
    current_row_end = ftell(fhtml);
    return 0;
}

int ha_html::rnd_init(bool scan)
{
    current_row_start = 0;
    current_row_end = data_start;
    return 0;
}

void ha_html::position(const uchar *record)
{
    *(ulong*)ref = current_row_start;
}

int ha_html::rnd_pos(uchar * buf, uchar *pos)
{
    memcpy(&current_row_end, pos, sizeof(current_row_end));
    return rnd_next(buf);
}
/*insert data*/
int ha_html::write_row(uchar *buf)
{
    if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_INSERT)//设置时间
        table->timestamp_field->set_time();
    if (table->next_number_field && buf == table->record[0]) {//设置自增，我们的引擎这里不需要
        int error;
        if ((error= update_auto_increment()))
            return error;
    }
    fseek(fhtml, -FOOTER_LEN, SEEK_END);//移动指针
    fprintf(fhtml, "<tr>");//写入字符
    my_bitmap_map *old_map = dbug_tmp_use_all_columns(table, table->read_set);
    my_ptrdiff_t offset = (my_ptrdiff_t)(buf-table->record[0]);//确定下一行offset
    for (uint i = 0; i < table->s->fields; i++) {
        Field *field = table->field[i];
        field->move_field_offset(offset);//移动指针
        if(field->is_null())//如果为空则写入<td/>
            fprintf(fhtml, "<td/>");
        else {//如果不为空，则写入列值和标签<td>
            char tmp_buf[1024];
            String tmp(tmp_buf, sizeof(tmp_buf), &my_charset_bin);
            String *val = field->val_str(&tmp, &tmp);
            fprintf(fhtml, "<td>");
            write_html(fhtml, val->c_ptr());
            fprintf(fhtml, "</td>");
        }
        field->move_field_offset(-offset);//将指针移动回去
    }
    dbug_tmp_restore_column_map(table->read_set, old_map);//恢复field的readset
    if(fprintf(fhtml, "</tr>\n" FOOTER) < 6 + FOOTER_LEN)
        return errno;
    else
        return 0;
}
/*Compared to write_row() , our delete_row() method is much simpler. MySQL can
only call delete_row() for the current row. And because we know where it starts—at
the current_row_start offset, we can even assert this fact—and we know where it
ends, we can easily comment the complete row out, "deleting" it from the HTML table.*/
int ha_html::delete_row(const uchar *buf)
{
    assert(current_row_start);
    fseek(fhtml, current_row_start, SEEK_SET);
    fprintf(fhtml, "<!--");
    fseek(fhtml, current_row_end-4, SEEK_SET);
    fprintf(fhtml, "-->\n");
    return 0;
}
/*The update_row() method is simple too. Just like write_row() it starts by updating
the TIMESTAMP field, but after that we can simply delete the old row and insert
the new one at the end of the table. We just need to remember to remove the
TIMESTAMP_AUTO_SET_ON_INSERT bit to avoid unnecessary TIMESTAMP updates;
MySQL will restore it for the next statement automatically. Because we have
remembered the original "end of table" offset at the beginning of the statement,
we do not need to worry that our table scan will read newly inserted data.*/
int ha_html::update_row(const uchar *old_data, uchar *new_data)
{
    assert(current_row_start);
    if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_UPDATE)
        table->timestamp_field->set_time();
    delete_row(old_data);
    table->timestamp_field_type = (timestamp_auto_set_type)
    (table->timestamp_field_type & ~TIMESTAMP_AUTO_SET_ON_INSERT);
    return write_row(new_data);
}

int ha_html::optimize(THD* thd, HA_CHECK_OPT* check_opt)
{
    return HA_ADMIN_TRY_ALTER;
}

int ha_html::analyze(THD* thd, HA_CHECK_OPT* check_opt)
{
    return 0;
}

int ha_html::info(uint flag)
{
    if (flag & HA_STATUS_VARIABLE)
        stats.records = 10;
    return 0;
}

int ha_html::external_lock(THD *thd, int lock_type)
{
    if (lock_type != F_UNLCK) {
        fseek(fhtml, -FOOTER_LEN, SEEK_END);
        data_end = ftell(fhtml);
    }
    else
        fflush(fhtml);
    return 0;
}

void write_html(FILE *f, char *str)
{
    for (; *str; str++)
    {
        switch (*str) {
            case '<': fputs("&lt;", f); break;
            case '>': fputs("&gt;", f); break;
            case '&': fputs("&amp;", f); break;
            case '-': fputs("&ndash;", f); break;
            default: fputc(*str, f); break;
        }
    }
}

int read_html(FILE *f, char *buf)
{
    int c;
    char *start = buf;
    while ((c = fgetc(f)) != '<') {
        if (c == '&') {
            char entity[21];
            fscanf(f, "%20[^;];", entity);
            if (strcmp(entity, "lt") == 0) { c = '<'; }
            else
            if (strcmp(entity, "gt") == 0) { c = '>'; }
            else
            if (strcmp(entity, "amp") == 0) { c = '&'; }
            else
            if (strcmp(entity, "ndash") == 0) { c = '-'; }
        }
        *buf++ = c;
    }
    ungetc(c, f);
    *buf=0;
    return buf - start;
}

int skip_html(FILE *f, const char *pattern)
{
    long old=ftell(f);
    fscanf(f, pattern);
    return ftell(f) - old;
}