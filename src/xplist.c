/*
 * plist.c
 * XML plist implementation
 *
 * Copyright (c) 2008 Jonathan Beck All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 */


#include <string.h>
#include <assert.h>
#include "utils.h"
#include "plist.h"
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>


#include <libxml/parser.h>
#include <libxml/tree.h>


const char *plist_base = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
<plist version=\"1.0\">\n\
</plist>\0";


/** Formats a block of text to be a given indentation and width.
 * 
 * The total width of the return string will be depth + cols.
 *
 * @param buf The string to format.
 * @param cols The number of text columns for returned block of text.
 * @param depth The number of tabs to indent the returned block of text.
 *
 * @return The formatted string.
 */
static char *format_string(const char *buf, int cols, int depth)
{
	int colw = depth + cols + 1;
	int len = strlen(buf);
	int nlines = len / cols + 1;
	char *new_buf = (char *) malloc(nlines * colw + depth + 1);
	int i = 0;
	int j = 0;

	assert(cols >= 0);
	assert(depth >= 0);

	// Inserts new lines and tabs at appropriate locations
	for (i = 0; i < nlines; i++) {
		new_buf[i * colw] = '\n';
		for (j = 0; j < depth; j++)
			new_buf[i * colw + 1 + j] = '\t';
		memcpy(new_buf + i * colw + 1 + depth, buf + i * cols, cols);
	}
	new_buf[len + (1 + depth) * nlines] = '\n';

	// Inserts final row of indentation and termination character
	for (j = 0; j < depth; j++)
		new_buf[len + (1 + depth) * nlines + 1 + j] = '\t';
	new_buf[len + (1 + depth) * nlines + depth + 1] = '\0';

	return new_buf;
}



struct xml_node {
	xmlNodePtr xml;
	uint32_t depth;
};

/** Creates a new plist XML document.
 * 
 * @return The plist XML document.
 */
static xmlDocPtr new_xml_plist()
{
	char *plist = strdup(plist_base);
	xmlDocPtr plist_xml = xmlReadMemory(plist, strlen(plist), NULL, NULL, 0);

	if (!plist_xml)
		return NULL;

	free(plist);

	return plist_xml;
}

/** Destroys a previously created XML document.
 *
 * @param plist The XML document to destroy.
 */
static void free_plist(xmlDocPtr plist)
{
	if (!plist)
		return;

	xmlFreeDoc(plist);
}

static void node_to_xml(GNode * node, gpointer xml_struct)
{
	if (!node)
		return;

	struct xml_node *xstruct = (struct xml_node *) xml_struct;
	plist_data_t node_data = plist_get_data(node);

	xmlNodePtr child_node = NULL;
	char isStruct = FALSE;

	const gchar *tag = NULL;
	const gchar *val = NULL;

	switch (node_data->type) {
	case PLIST_BOOLEAN:
		{
			if (node_data->boolval)
				tag = "true";
			else
				tag = "false";
		}
		break;

	case PLIST_UINT:
		tag = "integer";
		val = g_strdup_printf("%lu", (long unsigned int) node_data->intval);
		break;

	case PLIST_REAL:
		tag = "real";
		val = g_strdup_printf("%Lf", (long double) node_data->realval);
		break;

	case PLIST_STRING:
		tag = "string";
		val = g_strdup(node_data->strval);
		break;

	case PLIST_UNICODE:
		tag = "string";
		val = g_strdup((gchar *) node_data->unicodeval);
		break;

	case PLIST_KEY:
		tag = "key";
		val = g_strdup((gchar *) node_data->strval);
		break;

	case PLIST_DATA:
		tag = "data";
		gchar *valtmp = g_base64_encode(node_data->buff, node_data->length);
		val = format_string(valtmp, 68, xstruct->depth);
		g_free(valtmp);
		break;
	case PLIST_ARRAY:
		tag = "array";
		isStruct = TRUE;
		break;
	case PLIST_DICT:
		tag = "dict";
		isStruct = TRUE;
		break;
	case PLIST_DATE:			//TODO : handle date tag
	default:
		break;
	}

	int i = 0;
	for (i = 0; i < xstruct->depth; i++) {
		xmlNodeAddContent(xstruct->xml, "\t");
	}
	child_node = xmlNewChild(xstruct->xml, NULL, tag, val);
	xmlNodeAddContent(xstruct->xml, "\n");
	g_free(val);

	//add return for structured types
	if (node_data->type == PLIST_ARRAY || node_data->type == PLIST_DICT)
		xmlNodeAddContent(child_node, "\n");

	if (isStruct) {
		struct xml_node child = { child_node, xstruct->depth + 1 };
		g_node_children_foreach(node, G_TRAVERSE_ALL, node_to_xml, &child);
	}
	//fix indent for structured types
	if (node_data->type == PLIST_ARRAY || node_data->type == PLIST_DICT) {

		for (i = 0; i < xstruct->depth; i++) {
			xmlNodeAddContent(child_node, "\t");
		}
	}

	return;
}

void xml_to_node(xmlNodePtr xml_node, plist_t * plist_node)
{
	xmlNodePtr node = NULL;

	for (node = xml_node->children; node; node = node->next) {

		while (node && !xmlStrcmp(node->name, "text"))
			node = node->next;
		if (!node)
			break;

		plist_data_t data = plist_new_plist_data();
		plist_t subnode = plist_new_node(data);
		if (*plist_node)
			g_node_append(*plist_node, subnode);
		else
			*plist_node = subnode;

		if (!xmlStrcmp(node->name, "true")) {
			data->boolval = 1;
			data->type = PLIST_BOOLEAN;
			data->length = 1;
			continue;
		}

		if (!xmlStrcmp(node->name, "false")) {
			data->boolval = 0;
			data->type = PLIST_BOOLEAN;
			data->length = 1;
			continue;
		}

		if (!xmlStrcmp(node->name, "integer")) {
			char *strval = xmlNodeGetContent(node);
			data->intval = g_ascii_strtoull(strval, NULL, 0);
			data->type = PLIST_UINT;
			data->length = 8;
			continue;
		}

		if (!xmlStrcmp(node->name, "real")) {
			char *strval = xmlNodeGetContent(node);
			data->realval = atof(strval);
			data->type = PLIST_REAL;
			data->length = 8;
			continue;
		}

		if (!xmlStrcmp(node->name, "date"))
			continue;			//TODO : handle date tag

		if (!xmlStrcmp(node->name, "string")) {
			data->strval = strdup(xmlNodeGetContent(node));
			data->type = PLIST_STRING;
			data->length = strlen(data->strval);
			continue;
		}

		if (!xmlStrcmp(node->name, "key")) {
			data->strval = strdup(xmlNodeGetContent(node));
			data->type = PLIST_KEY;
			data->length = strlen(data->strval);
			continue;
		}

		if (!xmlStrcmp(node->name, "data")) {
			gsize size = 0;
			data->buff = g_base64_decode(xmlNodeGetContent(node), &size);
			data->length = size;
			data->type = PLIST_DATA;
			continue;
		}

		if (!xmlStrcmp(node->name, "array")) {
			data->type = PLIST_ARRAY;
			xml_to_node(node, &subnode);
			continue;
		}

		if (!xmlStrcmp(node->name, "dict")) {
			data->type = PLIST_DICT;
			xml_to_node(node, &subnode);
			continue;
		}
	}
}

void plist_to_xml(plist_t plist, char **plist_xml, uint32_t * length)
{
	if (!plist || !plist_xml || *plist_xml)
		return;
	xmlDocPtr plist_doc = new_xml_plist();
	xmlNodePtr root_node = xmlDocGetRootElement(plist_doc);
	struct xml_node root = { root_node, 0 };

	node_to_xml(plist, &root);

	xmlDocDumpMemory(plist_doc, (xmlChar **) plist_xml, length);
}

void plist_from_xml(const char *plist_xml, uint32_t length, plist_t * plist)
{
	xmlDocPtr plist_doc = xmlReadMemory(plist_xml, length, NULL, NULL, 0);
	xmlNodePtr root_node = xmlDocGetRootElement(plist_doc);

	xml_to_node(root_node, plist);
}
