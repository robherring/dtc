/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

#include "dtc.h"
#include "srcpos.h"

extern FILE *yyin;
extern int yyparse(void);
extern YYLTYPE yylloc;

struct dt_info *parser_output;
bool treesource_error;

struct dt_info *dt_from_source(const char *fname)
{
	parser_output = NULL;
	treesource_error = false;

	srcfile_push(fname);
	yyin = current_srcfile->f;
	yylloc.file = current_srcfile;

	if (yyparse() != 0)
		die("Unable to parse input tree\n");

	if (treesource_error)
		die("Syntax error parsing input tree\n");

	return parser_output;
}

static void write_prefix(FILE *f, int level)
{
	int i;

	for (i = 0; i < level; i++)
		fputc('\t', f);
}

static bool isstring(char c)
{
	return (isprint((unsigned char)c)
		|| (c == '\0')
		|| strchr("\a\b\t\n\v\f\r", c));
}

static void write_propval_string(FILE *f, const char *s, size_t len)
{
	const char *end = s + len - 1;
	assert(*end == '\0');

	fprintf(f, "\"");
	while (s < end) {
		char c = *s++;
		switch (c) {
		case '\a':
			fprintf(f, "\\a");
			break;
		case '\b':
			fprintf(f, "\\b");
			break;
		case '\t':
			fprintf(f, "\\t");
			break;
		case '\n':
			fprintf(f, "\\n");
			break;
		case '\v':
			fprintf(f, "\\v");
			break;
		case '\f':
			fprintf(f, "\\f");
			break;
		case '\r':
			fprintf(f, "\\r");
			break;
		case '\\':
			fprintf(f, "\\\\");
			break;
		case '\"':
			fprintf(f, "\\\"");
			break;
		case '\0':
			fprintf(f, "\", \"");
			break;
		default:
			if (isprint((unsigned char)c))
				fprintf(f, "%c", c);
			else
				fprintf(f, "\\x%02"PRIx8, c);
		}
	}
	fprintf(f, "\"");
}

static void write_propval_int(FILE *f, const char *p, size_t len, size_t width)
{
	const char *end = p + len - (width - 1);
	assert(len % width == 0);

	for (; p < end; p += width) {
		switch (width) {
		case 1:
			fprintf(f, " %02"PRIx8, *(const uint8_t*)p);
			break;
		case 2:
			fprintf(f, " 0x%02"PRIx16, fdt16_to_cpu(*(const fdt16_t*)p));
			break;
		case 4:
			fprintf(f, " 0x%02"PRIx32, fdt32_to_cpu(*(const fdt32_t*)p));
			break;
		case 8:
			fprintf(f, " 0x%02"PRIx64, fdt64_to_cpu(*(const fdt64_t*)p));
			break;
		}
	}
}

static struct marker* next_type_marker(struct marker *m)
{
	while (m && (m->type == LABEL || m->type == REF_PHANDLE || m->type == REF_PATH))
		m = m->next;
	return m;
}

const char * delim_start[] = {
	[MARKER_UINT8] = "[",
	[MARKER_UINT16] = "/bits/ 16 <",
	[MARKER_UINT32] = "<",
	[MARKER_UINT64] = "/bits/ 64 <",
	[MARKER_STRING] = "",
};
const char * delim_end[] = {
	[MARKER_UINT8] = " ]",
	[MARKER_UINT16] = " >",
	[MARKER_UINT32] = " >",
	[MARKER_UINT64] = " >",
	[MARKER_STRING] = "",
};
static void write_propval(FILE *f, struct property *prop)
{
	size_t len = prop->val.len;
	size_t chunk_len;
	struct marker *next_type, *m = prop->val.markers;
	struct marker dummy_marker;
	int nnotstring = 0, nnul = 0;
	int nnotstringlbl = 0, nnotcelllbl = 0;
	int i;
	enum markertype emit_type = MARKER_NONE;

	if (len == 0) {
		fprintf(f, ";\n");
		return;
	}

	fprintf(f, " = ");

	next_type = next_type_marker(m);
	chunk_len = (next_type ? next_type->offset : len);
	if (chunk_len) {
		/* data type information missing, need to guess */
		const char *p = prop->val.val;
		for (i = 0; i < chunk_len; i++) {
			if (! isstring(p[i]))
				nnotstring++;
			if (p[i] == '\0')
				nnul++;
		}

		for_each_marker_of_type(m, LABEL) {
			if (m->offset >= chunk_len)
				break;
			if ((m->offset > 0) && (prop->val.val[m->offset - 1] != '\0'))
				nnotstringlbl++;
			if ((m->offset % sizeof(cell_t)) != 0)
				nnotcelllbl++;
		}

		if ((p[chunk_len-1] == '\0') && (nnotstring == 0) && (nnul < (chunk_len-nnul))
		    && (nnotstringlbl == 0)) {
			dummy_marker.type = MARKER_STRING;
		} else if (((chunk_len % sizeof(cell_t)) == 0) && (nnotcelllbl == 0)) {
			dummy_marker.type = MARKER_UINT32;
		} else {
			dummy_marker.type = MARKER_UINT8;
		}

		dummy_marker.next = prop->val.markers;
		dummy_marker.offset = 0;
		dummy_marker.ref = NULL;
		m = &dummy_marker;
	}

	for_each_marker(m) {
		chunk_len = (m->next ? m->next->offset : len) - m->offset;
		const char *p = &prop->val.val[m->offset];

		switch(m->type) {
		case MARKER_UINT8:
		case MARKER_UINT16:
		case MARKER_UINT32:
		case MARKER_UINT64:
		case MARKER_STRING:
			if (emit_type != MARKER_NONE)
				fprintf(f, "%s, ", delim_end[emit_type]);
			emit_type = m->type;
			fprintf(f, "%s", delim_start[emit_type]);
			break;
		case LABEL:
			fprintf(f, " %s:", m->ref);
			break;
		default:
			break;
		}

		if (chunk_len <= 0)
			continue;

		switch(emit_type) {
		case MARKER_UINT16:
			write_propval_int(f, p, chunk_len, 2);
			break;
		case MARKER_UINT32:
			write_propval_int(f, p, chunk_len, 4);
			break;
		case MARKER_UINT64:
			write_propval_int(f, p, chunk_len, 8);
			break;
		case MARKER_STRING:
			write_propval_string(f, p, chunk_len);
			break;
		default:
			write_propval_int(f, p, chunk_len, 1);
		}
	}

	fprintf(f, "%s;\n", delim_end[emit_type] ? : "");
}

static void write_tree_source_node(FILE *f, struct node *tree, int level)
{
	struct property *prop;
	struct node *child;
	struct label *l;

	write_prefix(f, level);
	for_each_label(tree->labels, l)
		fprintf(f, "%s: ", l->label);
	if (tree->name && (*tree->name))
		fprintf(f, "%s {\n", tree->name);
	else
		fprintf(f, "/ {\n");

	for_each_property(tree, prop) {
		write_prefix(f, level+1);
		for_each_label(prop->labels, l)
			fprintf(f, "%s: ", l->label);
		fprintf(f, "%s", prop->name);
		write_propval(f, prop);
	}
	for_each_child(tree, child) {
		fprintf(f, "\n");
		write_tree_source_node(f, child, level+1);
	}
	write_prefix(f, level);
	fprintf(f, "};\n");
}


void dt_to_source(FILE *f, struct dt_info *dti)
{
	struct reserve_info *re;

	fprintf(f, "/dts-v1/;\n\n");

	for (re = dti->reservelist; re; re = re->next) {
		struct label *l;

		for_each_label(re->labels, l)
			fprintf(f, "%s: ", l->label);
		fprintf(f, "/memreserve/\t0x%016llx 0x%016llx;\n",
			(unsigned long long)re->address,
			(unsigned long long)re->size);
	}

	write_tree_source_node(f, dti->dt, 0);
}

