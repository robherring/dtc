// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libfdt - Flat Device Tree manipulation
 *	Tests that fdt_next_subnode() works as expected
 *
 * Copyright (C) 2013 Google, Inc
 *
 * Copyright (C) 2007 David Gibson, IBM Corporation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <libfdt.h>

#include "tests.h"
#include "testdata.h"

static void delete_domain_nodes(void *fdt, int keep)
{
	char nodestr[] = "/domains/domain@0";
	int offset, i;

	for (i = 0; i < 10; i++) {
		nodestr[strlen(nodestr) - 1] = '0' + i;
		offset = fdt_path_offset(fdt, nodestr);
		if (offset < 0)
			continue;

		if (i == keep)
			continue;

		fdt_del_node(fdt, offset);
	}
}

static int move_node_to_parent(void *fdt, int keep)
{
	char nodestr[] = "/domains/domain@0";
	int offset, parentoffset;

	nodestr[strlen(nodestr) - 1] = '0' + keep;
	offset = fdt_path_offset(fdt, nodestr);
	parentoffset = fdt_path_offset(fdt, "/domains");
	fdt_move_up_node(fdt, parentoffset, offset);

	return fdt_move_up_node(fdt, 0, parentoffset);

}


int main(int argc, char *argv[])
{
	void *fdt;
	int off;

	test_init(argc, argv);
	if (argc != 2)
		CONFIG("Usage: %s <dtb file>", argv[0]);

	fdt = load_blob(argv[1]);
	if (!fdt)
		FAIL("No device tree available");

	delete_domain_nodes(fdt, 0);

	off = move_node_to_parent(fdt, 0);
	printf("offset = %d\n", off);

	save_blob("test_move_nodes.dtb", fdt);

	PASS();
}
