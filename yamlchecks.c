// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright Arm Holdings.  2017, 2019
 * (C) Copyright Linaro, Ltd. 2018
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

//#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "dtc.h"
#include "srcpos.h"

static PyObject *dtschema;

static PyObject *propval_int(struct marker *markers, char *data, int len, int width)
{
	PyObject *pyo;
	int off, start_offset = markers->offset;

	assert(len % width == 0);

	pyo = PyObject_CallMethod(dtschema, "int_list", "O i", PyList_New(0), width*8);
	PyErr_Print();

	for (off = 0; off < len; off += width) {
		PyObject *py_long;
		struct marker *m;

		switch(width) {
		case 1:
			py_long = PyLong_FromUnsignedLong(*(uint8_t*)(data + off));
			break;
		case 2:
			py_long = PyLong_FromUnsignedLong(fdt16_to_cpu(*(fdt16_t*)(data + off)));
			break;
		case 4:
			py_long = PyLong_FromUnsignedLong(fdt32_to_cpu(*(fdt32_t*)(data + off)));
			m = markers;
			for_each_marker_of_type(m, REF_PHANDLE) {
				if (m->offset == (start_offset + off)) {
					py_long = PyObject_CallMethodObjArgs(dtschema, PyUnicode_FromString("phandle_int"), py_long, NULL);
//					PyErr_Print();
					break;
				}
			}
			break;
		case 8:
			py_long = PyLong_FromUnsignedLongLong(fdt64_to_cpu(*(fdt64_t*)(data + off)));
			break;
		}

		PyList_Append(pyo, py_long);
		Py_DECREF(py_long);
	}

	return pyo;
}

static PyObject *prop_to_value(struct property *prop)
{
	PyObject *pyo_val;
	int len = prop->val.len;
	struct marker *m = prop->val.markers;

	if (!prop->val.len)
		Py_RETURN_TRUE;

	pyo_val = PyList_New(0);

	for_each_marker(m) {
		PyObject *pyo;
		int chunk_len;
		char *data = &prop->val.val[m->offset];

		if (m->type < TYPE_UINT8)
			continue;

		chunk_len = type_marker_length(m) ? : len;
		assert(chunk_len > 0);
		len -= chunk_len;

		switch(m->type) {
		case TYPE_UINT16:
			pyo = propval_int(m, data, chunk_len, 2);
			break;
		case TYPE_UINT32:
			pyo = propval_int(m, data, chunk_len, 4);
			break;
		case TYPE_UINT64:
			pyo = propval_int(m, data, chunk_len, 8);
			break;
		case TYPE_STRING:
			pyo = PyUnicode_FromString(data);
			break;
		default:
			pyo = propval_int(m, data, chunk_len, 1);
			break;
		}

		PyList_Append(pyo_val, pyo);
		Py_DECREF(pyo);
	}

	return pyo_val;
}

static void add_src_pos(PyObject *dict, struct srcpos *srcpos, const char *name)
{
	char src[1024];
	PyObject *src_obj;

	if (!srcpos || !dict)
		return;

	snprintf(src, sizeof(src), "%s/%s:%d", srcpos->file->dir,
		srcpos->file->name, srcpos->first_line);

	src_obj = PyUnicode_FromString(src);
	PyDict_SetItemString(dict, name, src_obj);
	Py_DECREF(src_obj);
}

static void node_to_dict(PyObject *pyo, struct node *node)
{
	struct property *p;
	struct node *child;
	PyObject *src_dict;

	src_dict = PyDict_New();
	PyDict_SetItemString(pyo, "$srcfile", src_dict);
	Py_DECREF(src_dict);

	add_src_pos(src_dict, node->srcpos, strlen(node->name) ? node->name : "/");

	for_each_property(node, p) {
		PyObject *val = prop_to_value(p);

		add_src_pos(src_dict, p->srcpos, p->name);

		PyDict_SetItemString(pyo, p->name, val);
		Py_DECREF(val);
	}
	for_each_child(node, child) {
		PyObject *pyo_node = PyDict_New();

		PyDict_SetItemString(pyo, child->name, pyo_node);
		Py_DECREF(pyo_node);
		node_to_dict(pyo_node, child);
	}
}


void dt_to_python(struct dt_info *dti)
{
	PyObject *dt;

	Py_Initialize();

	dtschema = PyImport_ImportModule("dtschema");
	if (!dtschema || !PyObject_HasAttrString(dtschema, "check_tree"))
		goto err;


	dt = PyList_New(1);
	if (!dt)
		goto err;

	PyList_SetItem(dt, 0, PyDict_New());

	node_to_dict(PyList_GetItem(dt, 0), dti->dt);

//	PyObject_Print(dt, stderr, 0);

//	PyObject_CallMethod(dtschema, "check_tree", "Os", dt, "/home/rob/proj/git/dtc/test.yaml");
	PyObject_CallMethod(dtschema, "check_tree", "O", dt);

	//PyErr_Print();

//	PyObject_Print(schema, stderr, 0);

//	PyObject_CallObject(pFunc, pArgs);
//	PyRun_SimpleString(

err:
	PyErr_Print();
	Py_Finalize();
}
