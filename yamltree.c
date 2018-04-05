/*
 * (C) Copyright Arm Holdings.  2017
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
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

#include <stdlib.h>
#include <yaml.h>
#include "dtc.h"
#include "srcpos.h"

char *yaml_error_name[] = {
	[YAML_NO_ERROR] = "no error",
	[YAML_MEMORY_ERROR] = "memory error",
	[YAML_READER_ERROR] = "reader error",
	[YAML_SCANNER_ERROR] = "scanner error",
	[YAML_PARSER_ERROR] = "parser error",
	[YAML_COMPOSER_ERROR] = "composer error",
	[YAML_WRITER_ERROR] = "writer error",
	[YAML_EMITTER_ERROR] = "emitter error",
};

#define yaml_die(emitter) die("yaml '%s': %s in %s, line %i", yaml_error_name[(emitter)->error], (emitter)->problem, __func__, __LINE__)

static bool isstring(char c)
{
	return (isprint((unsigned char)c)
		|| (c == '\0')
		|| strchr("\a\b\t\n\v\f\r", c));
}

static void yaml_propval_int(yaml_emitter_t *emitter, char *data, int len, int width)
{
	yaml_event_t event;
	char *dataend = data + len;
	void *tag;
	char buf[32];

	if (len % width) {
		fprintf(stderr, "Warning: Property data length %i isn't a multiple of %i", len, width);
		width = 1;
	}

	switch(width) {
		case 1: tag = "!u8"; break;
		case 2: tag = "!u16"; break;
		case 8: tag = "!u64"; break;
		case 4: tag = "!u32"; break;
		default:
			die("Invalid width %i", width);
	}

	yaml_sequence_start_event_initialize(&event, NULL,
		(yaml_char_t *)tag, width == 4, YAML_FLOW_SEQUENCE_STYLE);
	if (!yaml_emitter_emit(emitter, &event))
		yaml_die(emitter);

	for (; data < dataend; data += width) {
		switch(width) {
		case 1:
			sprintf(buf, "0x%"PRIx8, *(uint8_t*)data);
			break;
		case 2:
			sprintf(buf, "0x%"PRIx16, fdt16_to_cpu(*(fdt16_t*)data));
			break;
		case 4:
			sprintf(buf, "0x%"PRIx32, fdt32_to_cpu(*(fdt32_t*)data));
			break;
		case 8:
			sprintf(buf, "0x%"PRIx64, fdt64_to_cpu(*(fdt64_t*)data));
			break;
		}

		yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t*)YAML_INT_TAG, (yaml_char_t *)buf,
			strlen(buf), 1, 1, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(emitter, &event))
			yaml_die(emitter);
	}

	yaml_sequence_end_event_initialize(&event);
	if (!yaml_emitter_emit(emitter, &event))
		yaml_die(emitter);
}

static void yaml_propval_string(yaml_emitter_t *emitter, char *str, int len)
{
	yaml_event_t event;
	int i;

	assert(str[len-1] == '\0');

	/* Make sure the entire string is in the lower 7-bit ascii range */
	for (i = 0; i < len; i++) {
		if (!isascii(str[i])) {
			fprintf(stderr, "Warning: non-ASCII character(s) in property string\n");
			yaml_propval_int(emitter, str, len, 1);
			return;
		}
	}

	yaml_scalar_event_initialize(&event, NULL,
		(yaml_char_t *)YAML_STR_TAG, (yaml_char_t*)str,
		len-1, 0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
	if (!yaml_emitter_emit(emitter, &event))
		yaml_die(emitter);
}

static void yaml_propval(yaml_emitter_t *emitter, struct property *prop)
{
	yaml_event_t event;
	int len = prop->val.len;
	const char *p = prop->val.val;
	struct marker *m = prop->val.markers;
	int nnotstring = 0, nnul = 0;
	int nnotstringlbl = 0, nnotcelllbl = 0;
	int i;
	enum markertype emit_type;

	/* Emit the property name */
	yaml_scalar_event_initialize(&event, NULL,
		(yaml_char_t *)YAML_STR_TAG, (yaml_char_t*)prop->name,
		strlen(prop->name), 1, 1, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(emitter, &event))
		yaml_die(emitter);

	/* Boolean properties are easiest to deal with. Length is zero, so just emit 'true' */
	if (len == 0) {
		yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_BOOL_TAG,
			(yaml_char_t*)"true",
			strlen("true"), 1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(emitter, &event))
			yaml_die(emitter);
		return;
	}

	emit_type = MARKER_UINT8;
	if (m) {
		int nest_level = 1;
		yaml_sequence_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_SEQ_TAG, 1, YAML_FLOW_SEQUENCE_STYLE);
		if (!yaml_emitter_emit(emitter, &event))
			yaml_die(emitter);

		for_each_marker(m) {
			int chunk_len = (m->next ? m->next->offset : len) - m->offset;
			char *data = &prop->val.val[m->offset];

			switch(m->type) {
			case LABEL:
				break; /* Labels don't change the data type */
			default:
				emit_type = m->type;
			}

			if (chunk_len <= 0)
				continue;

			switch(emit_type) {
			case REF_PHANDLE:
				yaml_propval_int(emitter, data, chunk_len, 4);
				break;
			case MARKER_UINT16:
				yaml_propval_int(emitter, data, chunk_len, 2);
				break;
			case MARKER_UINT32:
				yaml_propval_int(emitter, data, chunk_len, 4);
				break;
			case MARKER_UINT64:
				yaml_propval_int(emitter, data, chunk_len, 8);
				break;
			case MARKER_STRING:
				yaml_propval_string(emitter, data, chunk_len);
				break;
			default:
				yaml_propval_int(emitter, data, chunk_len, 1);
				break;
			}
		}

		while (nest_level) {
			yaml_sequence_end_event_initialize(&event);
			if (!yaml_emitter_emit(emitter, &event))
				yaml_die(emitter);
			nest_level--;
		}
		return;
	}

	yaml_propval_int(emitter, prop->val.val, prop->val.len, 1);
	return;

	/* This is dead code */
	/* No useful markers, fallback to guessing datatypes */
	for (i = 0; i < len; i++) {
		if (! isstring(p[i]))
			nnotstring++;
		if (p[i] == '\0')
			nnul++;
	}

	m = prop->val.markers;
	for_each_marker_of_type(m, LABEL) {
		if ((m->offset > 0) && (prop->val.val[m->offset - 1] != '\0'))
			nnotstringlbl++;
		if ((m->offset % sizeof(cell_t)) != 0)
			nnotcelllbl++;
	}

	if ((p[len-1] == '\0') && (nnotstring == 0) && (nnul < (len-nnul))
	    && (nnotstringlbl == 0)) {
		yaml_propval_string(emitter, prop->val.val, prop->val.len);
		return;
	}

	if (((len % sizeof(cell_t)) == 0) && (nnotcelllbl == 0)) {
		yaml_propval_int(emitter, prop->val.val, prop->val.len, 4);
		return;
	}

	yaml_propval_int(emitter, prop->val.val, prop->val.len, 1);
}


static void yaml_tree(struct node *tree, yaml_emitter_t *emitter)
{
	struct property *prop;
	struct node *child;
	yaml_event_t event;

	if (tree->deleted)
		return;

	yaml_mapping_start_event_initialize(&event, NULL,
		(yaml_char_t *)YAML_MAP_TAG, 1, YAML_ANY_MAPPING_STYLE);
	if (!yaml_emitter_emit(emitter, &event))
		yaml_die(emitter);

	for_each_property(tree, prop)
		yaml_propval(emitter, prop);

	/* Loop over all the children, emitting them into the map */
	for_each_child(tree, child) {
		yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG, (yaml_char_t*)child->name,
			strlen(child->name), 1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(emitter, &event))
			yaml_die(emitter);
		yaml_tree(child, emitter);
	}

	yaml_mapping_end_event_initialize(&event);
	if (!yaml_emitter_emit(emitter, &event))
		yaml_die(emitter);
}

void dt_to_yaml(FILE *f, struct dt_info *dti)
{
	yaml_emitter_t emitter;
	yaml_event_t event;

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_output_file(&emitter, f);
	yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
	if (!yaml_emitter_emit(&emitter, &event))
		yaml_die(&emitter);

	yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 0);
	if (!yaml_emitter_emit(&emitter, &event))
		yaml_die(&emitter);

	yaml_sequence_start_event_initialize(&event, NULL, (yaml_char_t *)YAML_SEQ_TAG, 1, YAML_ANY_SEQUENCE_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		yaml_die(&emitter);

	yaml_tree(dti->dt, &emitter);

	yaml_sequence_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		yaml_die(&emitter);

	yaml_document_end_event_initialize(&event, 0);
	if (!yaml_emitter_emit(&emitter, &event))
		yaml_die(&emitter);

	yaml_stream_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		yaml_die(&emitter);

	yaml_emitter_delete(&emitter);
}
