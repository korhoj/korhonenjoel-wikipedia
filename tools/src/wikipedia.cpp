/*
 * This file is part of StarDict.
 *
 * StarDict is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * StarDict is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with StarDict.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "stdio.h"
#include "stdlib.h"
#include <locale.h>
#include <string.h>
#include <sys/stat.h>

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include <gtk/gtk.h>

#include <iostream>

// for systems where O_LARGEFILE is not defined
#ifndef O_LARGEFILE
#define O_LARGEFILE (0)
#endif

struct _worditem
{
    gchar *title;
    gchar *text;
};

gint stardict_strcmp(const gchar *s1, const gchar *s2)
{
    gint a;
	a = g_ascii_strcasecmp(s1, s2);
    if (a == 0)
        return strcmp(s1, s2);
    else
        return a;
}

gint comparefunc(gconstpointer a,gconstpointer b)
{
    gint x;
    x = stardict_strcmp(((struct _worditem *)a)->title,((struct _worditem *)b)->title);
    if (x == 0)
        return ((struct _worditem *)a)->text - ((struct _worditem *)b)->text;
    else
        return x;
}


typedef struct _ParseUserData {
	bool inpage;
	gchar *title;
	gchar *text;
	GArray *array;
} ParseUserData;

static void func_parse_start_element(GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer user_data, GError **error)
{
	ParseUserData *Data = (ParseUserData *)user_data;
	if (strcmp(element_name, "page")==0) {
		Data->inpage = true;
		Data->title = NULL;
		Data->text = NULL;
	}
}

static void func_parse_end_element(GMarkupParseContext *context,
                          const gchar         *element_name,
                          gpointer             user_data,
                          GError             **error)
{
	ParseUserData *Data = (ParseUserData *)user_data;
        if (strcmp(element_name, "page")==0) {
                Data->inpage = false;
		struct _worditem worditem;
		if (Data->title && Data->text) {
			worditem.title = Data->title;
			worditem.text = Data->text;
			g_array_append_val(Data->array, worditem);
		} else {
			g_free(Data->title);
			g_free(Data->text);
		}
        }

}

static void func_parse_text(GMarkupParseContext *context, const gchar *text, gsize text_len, gpointer user_data, GError **error)
{
	ParseUserData *Data = (ParseUserData *)user_data;
	if (!Data->inpage)
		return;
	const gchar *element = g_markup_parse_context_get_element(context);
        if (!element) {
		return;
	}
	if (strcmp(element, "title")==0) {
		Data->title = g_strndup(text, text_len);
	} else if (strcmp(element, "text")==0) { 
		Data->text = g_strndup(text, text_len);
	}
}

void convert(char *filename, char *wikiname, char *wikidate)
{
	printf("Processing file: %s\n",filename);

	if (wikiname == NULL) { // if user didn't specify all 3 params in cmdline, deduce from filename
		gchar** name_parts = g_strsplit(filename,"-",3);
	
		gchar wikiname_str[256];
		sprintf(wikiname_str, "%s", name_parts[0]);
		wikiname = wikiname_str;

		gchar wikidate_str[256];
		sprintf(wikidate_str, "%s", name_parts[1]);
		wikidate = wikidate_str;

		printf("  using wikiname: %s\n",wikiname);
		printf("  using wikidate: %s\n",wikidate);

		g_strfreev(name_parts);
	}

	#if (_FILE_OFFSET_BITS == 32)
	printf("_FILE_OFFSET_BITS is defined only as 32, need 64!\n");
	return;
	#endif

    g_print("Checking status of file '%s'\n",filename);
    struct stat stats;
    if (stat (filename, &stats) == -1)
    {
        g_print("File stat check error! Errno: %d\n", errno);
        g_print("Error desc: %s\n", strerror(errno));
        return;
    }

    g_print("File size: %'" G_GINT64_FORMAT "\n", stats.st_size);

    g_print("Opening file\n");
	int mmap_fd;
	if ((mmap_fd = open(filename, O_RDONLY | O_LARGEFILE)) < 0) {
        g_print("Open file failed! Errno: %d\n", errno);
        g_print("Error desc: %s\n", strerror(errno));
		return;
	}

	char *data;
	GArray *array = g_array_sized_new(FALSE,FALSE, sizeof(struct _worditem),20000);
	ParseUserData Data;
	Data.inpage=false;
	Data.array= array;
	GMarkupParser parser;
	parser.start_element = func_parse_start_element;
	parser.end_element = func_parse_end_element;
	parser.text = func_parse_text;
	parser.passthrough = NULL;
	parser.error = NULL;
	GMarkupParseContext* context = g_markup_parse_context_new(&parser, (GMarkupParseFlags)0, &Data, NULL);
    gint64 length;
    for (goffset offset = 0; offset < stats.st_size; offset += 10240000) {
		if (offset + 10240000 > stats.st_size) {
			length = stats.st_size - offset;
		} else {
			length = 10240000;
		}
		data = (char *)mmap( NULL, length, PROT_READ, MAP_SHARED, mmap_fd, offset);
		if (data == MAP_FAILED) {
            g_print("mmap failed! Errno: %d\n", errno);
            g_print("Error desc: %s\n", strerror(errno));
			return;
		}
		if (g_markup_parse_context_parse(context, data, length, NULL) == FALSE) {
			g_print("Parse error! Errno: %d\n", errno);
            g_print("Error desc: %s\n", strerror(errno));
			if (!g_utf8_validate(data, length, NULL))
                g_print("invalid UTF-8.\n");
			g_file_set_contents("error.xml", data, length, NULL);
            munmap(data, length);
			return;
		}
		munmap(data, length);

		float percent_done = (float)offset/stats.st_size;
        g_print(" at offset %'" G_GOFFSET_FORMAT
         " (%.2f%%)\n", offset, 100*percent_done);

		fflush(stdout);
	}
	g_markup_parse_context_end_parse(context, NULL);
	g_markup_parse_context_free(context);
	close(mmap_fd);

	g_print("Parse over!\n");

	g_array_sort(array,comparefunc);
	gchar idxfilename[256];
    gchar dicfilename[256];
	gchar ifofilename[256];
    sprintf(idxfilename, "wikipedia-%s-%s.idx", wikiname, wikidate);
    sprintf(dicfilename, "wikipedia-%s-%s.dict", wikiname, wikidate);
    sprintf(ifofilename, "wikipedia-%s-%s.ifo", wikiname, wikidate);
    FILE *idxfile = fopen(idxfilename,"w");
    FILE *dicfile = fopen(dicfilename,"w");
	//guint64 offset_old;
	guint32 offset_old;
    //guint64 tmpglong;
	guint32 tmpglong;
    struct _worditem *pworditem;
    gint definition_len;
    gulong i;
	g_print("Writing idx and dict file, processing %'i items\n", array->len);
    for (i=0; i<array->len; i++) {
        offset_old = ftell(dicfile);
        pworditem = &g_array_index(array, struct _worditem, i);

		if (i % 10000 == 0) {
			float percent_done = (float)i/array->len;
			g_print("  title:'%s', at %'li (%.2f%%)\n", pworditem->title, i, 100*percent_done);
		}

        definition_len = strlen(pworditem->text);
        fwrite(pworditem->text, 1 ,definition_len,dicfile);
        fwrite(pworditem->title,sizeof(gchar),strlen(pworditem->title)+1,idxfile);
        tmpglong = g_htonl(offset_old);
        //fwrite(&(tmpglong),sizeof(guint64),1,idxfile);
		fwrite(&(tmpglong),sizeof(guint32),1,idxfile);
        tmpglong = g_htonl(definition_len);
        //fwrite(&(tmpglong),sizeof(guint64),1,idxfile);
		fwrite(&(tmpglong),sizeof(guint32),1,idxfile);
		g_free(pworditem->text);
		g_free(pworditem->title);
    }
	long idxfilesize = ftell(idxfile);
    fclose(idxfile);
    fclose(dicfile);

	FILE *ifofile = fopen(ifofilename,"w");

    GDateTime* date_time_now = g_date_time_new_now_local();
	gchar *currdate = g_date_time_format (date_time_now, "%Y.%m.%d"); // YYYY.mm.dd
	gchar *content = g_strdup_printf("StarDict's dict ifo file\nversion=2.4.2\nwordcount=%d\nidxfilesize=%ld\nbookname=%s\ndescription=Made by Joel Korhonen and Hu Zheng. WikiPedia version: %s\ndate=%s\nsametypesequence=w\n", array->len, idxfilesize, wikiname, wikidate, currdate);
	fwrite(content, strlen(content), 1, ifofile);
	g_free(content);
    g_free(currdate);
    g_date_time_unref(date_time_now);

	fclose(ifofile);

    g_print("%s wordcount: %d\n", filename, array->len);
	g_array_free(array,TRUE);

	gchar command[256];
    sprintf(command, "dictzip %s", dicfilename);
	int result;
    result = system(command);
	if (result == -1) {
		g_print("system() error at dictzip! Errno: %d\n", errno);
        g_print("Error desc: %s\n", strerror(errno));
		return;
	}

	char dirname[256];
	sprintf(dirname, "stardict-wikipedia-%s-2.4.2-%s", wikiname,wikidate);
	sprintf(command, "mkdir %s", dirname);
	result = system(command);
	if (result == -1) {
		g_print("system() error at mkdir! Errno: %d\n", errno);
		g_print("Error desc: %s\n", strerror(errno));
		return;
	}
	sprintf(command, "mv %s %s", idxfilename, dirname);
	result = system(command);
	if (result == -1) {
		g_print("system() error at moving idx file! Errno: %d\n", errno);
		g_print("Error desc: %s\n", strerror(errno));
		return;
	}
	sprintf(command, "mv %s.dz %s", dicfilename, dirname);
	result = system(command);
	if (result == -1) {
		g_print("system() error at moving dz file! Errno: %d\n", errno);
		g_print("Error desc: %s\n", strerror(errno));
		return;
	}
	sprintf(command, "mv %s %s", ifofilename, dirname);
	result = system(command);
	if (result == -1) {
		g_print("system() error at moving ifo file! Errno: %d\n", errno);
		g_print("Error desc: %s\n", strerror(errno));
		return;
	}
}

int main(int argc,char * argv [])
{
	if (argc!=4 && argc!=2) {
		printf("please type this:\n./wikipedia zhwiki-20060303-pages-articles.xml zhwiki 20060303\n"
               " or ./wikipedia zhwiki-20060303-pages-articles.xml\n");
        return FALSE;
    }

	setlocale(LC_ALL, "");
	if (argc==4)
	    convert (argv[1], argv[2], argv[3]);
	else
		convert (argv[1], NULL, NULL);
	return FALSE;
}
