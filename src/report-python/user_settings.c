/*
    Copyright (C) 2014  Abrt team.
    Copyright (C) 2014  RedHat inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "common.h"
#include "internal_libreport.h"

PyObject *p_load_app_conf_file(PyObject *module, PyObject *args)
{
    const char *applicaton_name;
    if (!PyArg_ParseTuple(args, "s", &applicaton_name))
        return NULL;

    PyObject *dict = NULL;
    map_string_t *settings = new_map_string();
    if (!load_app_conf_file(applicaton_name, settings))
    {
        PyErr_SetString(PyExc_OSError, "Failed to load configuration file.");
        goto lacf_error;
    }

    dict = PyDict_New();
    if (dict == NULL)
        goto lacf_error;

    map_string_iter_t iter;
    const char *key = NULL;
    const char *value = NULL;
    init_map_string_iter(&iter, settings);
    while(next_map_string_iter(&iter, &key, &value))
        if (0 != PyDict_SetItemString(dict, key, PyUnicode_FromString(value)))
            goto lacf_error;

    free_map_string(settings);
    return dict;

lacf_error:
    Py_XDECREF(dict);
    free_map_string(settings);
    return NULL;
}

