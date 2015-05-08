#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib.h>
#include <config.h>
#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <glade/glade.h>
#include <GL/gl.h>

#include "main.h"
#include "glext2d.h"
#include "glext3d.h"
#include "socket.h"

#define NUM_PORTS 65536


void handler_port_init_interface()
{
	// disable transparency by default
	viz->transparent = FALSE;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viz->transparent_check_box), FALSE);

	// enable lighting by default
	viz->disable_lighting = FALSE;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viz->lighting_check_box), FALSE);

	// disable labels by default
	viz->draw_labels = FALSE;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viz->labels_check_box), FALSE);

	// enable/disable appropriate scaling bars
	gtk_widget_set_sensitive(viz->line_scale_range, FALSE);
	gtk_widget_set_sensitive(viz->line_scale_label, FALSE);
	gtk_widget_set_sensitive(viz->slice_scale_range, TRUE);
	gtk_widget_set_sensitive(viz->slice_scale_label, TRUE);

	// enable our set of ranges and labels for ports
	gtk_widget_set_sensitive(viz->port1_min_range, TRUE);
	gtk_widget_set_sensitive(viz->port1_max_range, TRUE);
	gtk_widget_set_sensitive(viz->port1_min_label, TRUE);
	gtk_widget_set_sensitive(viz->port1_max_label, TRUE);
	gtk_widget_set_sensitive(viz->port2_min_range, FALSE);
	gtk_widget_set_sensitive(viz->port2_max_range, FALSE);
	gtk_widget_set_sensitive(viz->port2_min_label, FALSE);
	gtk_widget_set_sensitive(viz->port2_max_label, FALSE);

	// set the correct labels on those ranges
	gtk_label_set_label(GTK_LABEL(viz->port1_min_label), "Min Port:");
	gtk_label_set_label(GTK_LABEL(viz->port1_max_label), "Max Port:");
	gtk_label_set_label(GTK_LABEL(viz->port2_min_label), "N/A:");
	gtk_label_set_label(GTK_LABEL(viz->port2_max_label), "N/A:");
}


coord_t *handler_port_get_node_coords(tree_node_t *node)
{
	return (&node->port.coords);
}


gboolean handler_port_check_2d_click(tree_node_t *node, GLfloat x_pos, GLfloat y_pos)
{
	// check if the click was within the boundries of the node
	if (x_pos >= node->port.coords.x_coord
			&& x_pos <= node->port.coords.x_coord + node->port.coords.x_width
			&& y_pos >= node->port.coords.y_coord
			&& y_pos <= node->port.coords.y_coord + node->port.coords.y_width) {
		return TRUE;
	} else {
		return FALSE;
	}
}


gboolean handler_port_check_visibility(tree_node_t *node)
{
	return (node->volume >= viz->threshold_min && 
		node->volume <= viz->threshold_max &&
		node->port.port_num >= viz->port1_min && 
		node->port.port_num <= viz->port1_max);
}


static inline gboolean handler_port_render_2d_node(gpointer node_in, gpointer null, gpointer data)
{
	tree_node_t *node = node_in;

	// return if the node is not within the thresholds
	if (!viz->handler.check_visibility(node)) {
		return FALSE;
	}

	// precalculate these to avoid redundant calculations
	GLfloat x_width = node->port.coords.x_coord + node->port.coords.x_width;
	GLfloat y_width = node->port.coords.y_coord + node->port.coords.y_width;

	GLfloat quad_strip_v[] = {
					node->port.coords.x_coord, node->port.coords.y_coord,
					x_width, node->port.coords.y_coord,
					node->port.coords.x_coord, y_width,
					x_width, y_width
				 };

	// set color and transparency
	if (!viz->transparent) {
		node->port.coords.color[3] = 255;
	}
	glColor4ubv(node->port.coords.color);

	// render our arrays of vertices
	glVertexPointer(2, GL_FLOAT, 0, quad_strip_v);
	glDrawArrays(GL_QUAD_STRIP, 0, 4);

	return FALSE;
}


void handler_port_render_2d_scene(void)
{
	GLfloat unit_size;
	guint i, num_ranges;
	ip_range_t *ranges;
	bound_t *bound;

	if (viz->mode.data_type == SRC_PORT) {
		bound = &viz->src_bound;
		ranges = viz->src_ranges;
		num_ranges = viz->num_src_ranges;
		unit_size = CUBE_SIZE  / (GLfloat) (viz->src_bound.max_x_coord - viz->src_bound.min_x_coord);
	} else if (viz->mode.data_type == DST_PORT) {
		bound = &viz->dst_bound;
		ranges = viz->dst_ranges;
		num_ranges = viz->num_dst_ranges;
		unit_size = CUBE_SIZE  / (GLfloat) (viz->dst_bound.max_x_coord - viz->dst_bound.min_x_coord);
	} else {
		return;
	}

	// draw a square outline
	glCallList(GREY_2D_SQUARE_CALL_LIST);

	// draw a white square representing each range over our grey base
	for (i = 0; i < num_ranges; ++i) {
		GLfloat x_coord = (GLfloat) ((ranges[i].x_coord - bound->min_x_coord) * unit_size) - 1.0;
		GLfloat y_coord = (GLfloat) ((ranges[i].y_coord - bound->min_y_coord) * unit_size) - 1.0;
		GLfloat width = (GLfloat) (ranges[i].width * unit_size);

		glBegin(GL_QUAD_STRIP);
		glNormal3f(0.0, 0.0, 1.0);
		glColor3ub(255, 255, 255);

		glVertex2f(x_coord, y_coord);
		glVertex2f(x_coord, y_coord + width);
		glVertex2f(x_coord + width, y_coord);
		glVertex2f(x_coord + width, y_coord + width);

		glEnd();
	}

	// derive address based on the quadtree coordinates of our bounding box
	struct in_addr low_addr;
	struct in_addr high_addr;
	range_reverse_quadtree(bound, &low_addr, &high_addr);

	gchar *low_str = g_strdup_printf(" %s", inet_ntoa(low_addr));
	gchar *high_str = g_strdup_printf(" %s", inet_ntoa(high_addr));

	// lower address range label
	glColor3f(1.0, 1.0, 1.0);
	glRasterPos2f(-1.0, -1.0);
	glListBase(viz->font_list_2d);
	glCallLists(strlen(low_str), GL_UNSIGNED_BYTE, low_str);

	// higher address range label
	glRasterPos2f(1.0, 1.0);
	glListBase(viz->font_list_2d);
	glCallLists(strlen(high_str), GL_UNSIGNED_BYTE, high_str);

	g_free(low_str);
	g_free(high_str);

	// render all our nodes!
	g_tree_foreach(viz->nodes, handler_port_render_2d_node, NULL);
}


static inline gboolean handler_port_render_3d_node(gpointer node_in, gpointer null, gpointer data)
{
	tree_node_t *node = node_in;

	// only draw the node if it is between the thresholds
	if (!viz->handler.check_visibility(node)) {
		return FALSE;
	}

	// determine the z_coord based on the current port range
	node->port.coords.z_coord = (GLfloat) ((GLfloat) ((COORD_SIZE / (GLfloat) (viz->port1_max - viz->port1_min))) * (node->port.port_num - viz->port1_min) * STANDARD_UNIT_SIZE) - 1.0;

	// determine the slice height based on the current scale
	node->port.coords.height = (GLfloat) gtk_range_get_value(GTK_RANGE(viz->slice_scale_range)) * STANDARD_UNIT_SIZE;

	// precalculate these to avoid redundant calculations
	GLfloat x_width = node->port.coords.x_coord + node->port.coords.x_width;
	GLfloat y_width = node->port.coords.y_coord + node->port.coords.y_width;
	GLfloat z_height = node->port.coords.z_coord + node->port.coords.height;

	// define our array of quadrilaterals
	GLfloat quads_v[] =     {
					// top face
					node->port.coords.x_coord, node->port.coords.y_coord, z_height,
					x_width, node->port.coords.y_coord, z_height,
					x_width, y_width, z_height,
					node->port.coords.x_coord, y_width, z_height,

					// "front" face
					x_width, node->port.coords.y_coord, node->port.coords.z_coord,
					x_width, y_width, node->port.coords.z_coord,
					x_width, y_width, z_height,
					x_width, node->port.coords.y_coord, z_height,

					// "back" face
					node->port.coords.x_coord, node->port.coords.y_coord, node->port.coords.z_coord,
					node->port.coords.x_coord, y_width, node->port.coords.z_coord,
					node->port.coords.x_coord, y_width, z_height,
					node->port.coords.x_coord, node->port.coords.y_coord, z_height,

					// "left" face
					node->port.coords.x_coord, node->port.coords.y_coord, node->port.coords.z_coord,
					x_width, node->port.coords.y_coord, node->port.coords.z_coord,
					x_width, node->port.coords.y_coord, z_height,
					node->port.coords.x_coord, node->port.coords.y_coord, z_height,

					// "right" face
					node->port.coords.x_coord, y_width, node->port.coords.z_coord,
					x_width, y_width, node->port.coords.z_coord,
					x_width, y_width, z_height,
					node->port.coords.x_coord, y_width, z_height,

					// bottom square at base of cube
					node->port.coords.x_coord, node->port.coords.y_coord, -1.0 + Z_HEIGHT_ADJ,
					x_width, node->port.coords.y_coord, -1.0 + Z_HEIGHT_ADJ,
					x_width, y_width, -1.0 + Z_HEIGHT_ADJ,
					node->port.coords.x_coord, y_width, -1.0 + Z_HEIGHT_ADJ
				};

	// set color and alpha transparency
	if (!viz->transparent) {
		node->port.coords.color[3] = 255;
	}
	glColor4ubv(node->port.coords.color);

	// render our arrays of vertices with given normals
	glNormalPointer(GL_FLOAT, 0, viz->quads_normal_3d);
	glVertexPointer(3, GL_FLOAT, 0, quads_v);
	glDrawArrays(GL_QUADS, 0, 24);


	// draw labels if necessary
	if (viz->draw_labels) {
		glDisable(GL_LIGHTING);

		gchar *node_label = g_strdup_printf("%s:%d - %d", inet_ntoa(node->port.addr), node->port.port_num, node->volume);

		// output the nodes label
		glColor3ubv(node->port.coords.color);
		glRasterPos3f(node->port.coords.x_coord + (node->port.coords.x_width / 2), node->port.coords.y_coord + (node->port.coords.y_width / 2), z_height + Z_LABEL_ADJ);
		glListBase(viz->font_list_3d);
		glCallLists(strlen(node_label), GL_UNSIGNED_BYTE, node_label);

		g_free(node_label);

		if (!viz->disable_lighting) {
			glEnable(GL_LIGHTING);
		}
	}

	return FALSE;
}


void handler_port_render_3d_scene(void)
{
	GLfloat unit_size;
	guint i, num_ranges;
	ip_range_t *ranges;
	bound_t *bound;

	if (viz->mode.data_type == SRC_PORT) {
		bound = &viz->src_bound;
		ranges = viz->src_ranges;
		num_ranges = viz->num_src_ranges;
		unit_size = CUBE_SIZE  / (GLfloat) (viz->src_bound.max_x_coord - viz->src_bound.min_x_coord);
	} else if (viz->mode.data_type == DST_PORT) {
		bound = &viz->dst_bound;
		ranges = viz->dst_ranges;
		num_ranges = viz->num_dst_ranges;
		unit_size = CUBE_SIZE  / (GLfloat) (viz->dst_bound.max_x_coord - viz->dst_bound.min_x_coord);
	} else {
		return;
	}

	// draw a white square representing each range over our grey base
	for (i = 0; i < num_ranges; ++i) {
		GLfloat x_coord = (GLfloat) ((ranges[i].x_coord - bound->min_x_coord) * unit_size) - 1.0;
		GLfloat y_coord = (GLfloat) ((ranges[i].y_coord - bound->min_y_coord) * unit_size) - 1.0;
		GLfloat width = (GLfloat) (ranges[i].width * unit_size);

		glBegin(GL_QUAD_STRIP);
		glNormal3f(0.0, 0.0, 1.0);
		glColor3ub(255, 255, 255);

		glVertex3f(x_coord, y_coord, -1.0 + (Z_HEIGHT_ADJ / 2.0));
		glVertex3f(x_coord, y_coord + width, -1.0 + (Z_HEIGHT_ADJ / 2.0));
		glVertex3f(x_coord + width, y_coord, -1.0 + (Z_HEIGHT_ADJ / 2.0));
		glVertex3f(x_coord + width, y_coord + width, -1.0 + (Z_HEIGHT_ADJ / 2.0));

		glEnd();
	}


	// white wire outline
	glCallList(WIRE_CUBE_CALL_LIST);


	// output our coord labels
	glDisable(GL_LIGHTING);

	// derive address based on the quadtree coordinates of our bounding box
	struct in_addr low_addr;
	struct in_addr high_addr;
	range_reverse_quadtree(bound, &low_addr, &high_addr);

	gchar *low_str = g_strdup_printf(" %s", inet_ntoa(low_addr));
	gchar *high_str = g_strdup_printf(" %s", inet_ntoa(high_addr));

	// lower address range label
	glColor3f(1.0, 1.0, 1.0);
	glRasterPos3f(-1.0, -1.0, -0.9);
	glListBase(viz->font_list_3d);
	glCallLists(strlen(low_str), GL_UNSIGNED_BYTE, low_str);

	// higher address range label
	glRasterPos3f(1.0, 1.0, -0.9);
	glListBase(viz->font_list_3d);
	glCallLists(strlen(high_str), GL_UNSIGNED_BYTE, high_str);

	g_free(low_str);
	g_free(high_str);

	glEnable(GL_LIGHTING);


	// output our z-axis labels
	glDisable(GL_LIGHTING);

	guint port_diff = viz->port1_max - viz->port1_min;

	gchar *label0 = g_strdup_printf(" %d", viz->port1_min);
	gchar *label1 = g_strdup_printf(" %d", viz->port1_min + port_diff * 1/4);
	gchar *label2 = g_strdup_printf(" %d  PORT", viz->port1_min + port_diff * 1/2);
	gchar *label3 = g_strdup_printf(" %d", viz->port1_min + port_diff * 3/4);
	gchar *label4 = g_strdup_printf(" %d", viz->port1_max);

	// output our labels
	glColor3f(1.0, 1.0, 1.0);

	glRasterPos3f(-1.0, 1.0, -1.0);
	glListBase(viz->font_list_3d);

	glCallLists(strlen(label0), GL_UNSIGNED_BYTE, label0);

	glRasterPos3f(-1.0, 1.0, -0.5);
	glListBase(viz->font_list_3d);
	glCallLists(strlen(label1), GL_UNSIGNED_BYTE, label1);

	glRasterPos3f(-1.0, 1.0, 0.0);
	glListBase(viz->font_list_3d);
	glCallLists(strlen(label2), GL_UNSIGNED_BYTE, label2);

	glRasterPos3f(-1.0, 1.0, 0.5);
	glListBase(viz->font_list_3d);
	glCallLists(strlen(label3), GL_UNSIGNED_BYTE, label3);

	glRasterPos3f(-1.0, 1.0, 1.0);
	glListBase(viz->font_list_3d);
	glCallLists(strlen(label4), GL_UNSIGNED_BYTE, label4);

	g_free(label0);
	g_free(label1);
	g_free(label2);
	g_free(label3);
	g_free(label4);

	glEnable(GL_LIGHTING);

	// disable the lighting if necessary for a performance increase
	if (viz->disable_lighting) {
		glDisable(GL_LIGHTING);
	}

	// give the wire outline a solid floor
	glCallList(GREY_3D_SQUARE_CALL_LIST);

	// render all our nodes!
	g_tree_foreach(viz->nodes, handler_port_render_3d_node, NULL);

	if (!viz->transparent) {
		glCallList(FLAMINGO_LOGO_CALL_LIST);
	}
}


gint handler_port_compare_nodes(tree_node_t *node_a, tree_node_t *node_b)
{
	guint mask_a = pow(2, 32 - node_a->port.network_bits) - 1;
	guint mask_b = pow(2, 32 - node_b->port.network_bits) - 1;
	guint addr_a = g_ntohl(node_a->port.addr.s_addr) & ~mask_a;
	guint addr_b = g_ntohl(node_b->port.addr.s_addr) & ~mask_b;

	// check if the addresses are less than or greater than the other
	if (addr_a < addr_b) {
		return -1;
	} else if (addr_a > addr_b) {
		return 1;
	}

	// if we get to here the addresses must be equal, so compare the ports
	if (node_a->port.port_num < node_b->port.port_num) {
		return -1;
	} else if (node_a->port.port_num > node_b->port.port_num) {
		return 1;
	}

	// if we get to here, both are identical
	return 0;
}


tree_node_t *handler_port_lookup_node_from_string(gchar *node_str)
{
	tree_node_t *node;
	gchar **addr_split;
	struct in_addr addr;

	node = g_new0(tree_node_t, 1);

	// split up the address and the port
	addr_split = g_strsplit(node_str, ":", 2);

	inet_aton(addr_split[0], &addr);

	node->port.addr.s_addr = addr.s_addr;
	node->port.port_num = atoi(addr_split[1]);

	g_strfreev(addr_split);

	return node;
}


void handler_port_input_process_nodes(GTree *tree, data_record_t *data, guint len)
{
	// loop through all received entries
	data_record_t *curr_record;
	guint i, num_entries = len / sizeof(data_record_t);

	for (i = 0; i < num_entries; ++i) {
		curr_record = (data_record_t *) data + i;

		// allocate mem for a new node
		tree_node_t *node = g_new0(tree_node_t, 1);

		// assign the node's vars
		node->port.addr.s_addr = curr_record->address;
		node->port.network_bits = g_ntohl(curr_record->length);
		node->port.port_num = g_ntohl(curr_record->port);
		node->port.origin_as = g_ntohl(curr_record->originas);
		node->volume = g_ntohl(curr_record->count);

		// assign our RGB colors of the node as specified by the server
		node->port.coords.color[0] = curr_record->red;
		node->port.coords.color[1] = curr_record->green;
		node->port.coords.color[2] = curr_record->blue;

		// calculate the transparency of the node based on its network bits
		node->port.coords.color[3] = 255 - ((24 - node->port.network_bits) * 10);

		// calculate the quadtree coordinates
		node->port.coords.x_coord = 0.0;
		node->port.coords.y_coord = 0.0;

		// calculate width of the node
		guint num_of_quad_divisions = (node->port.network_bits + 1) / 2.0;
		GLfloat node_x_width = COORD_SIZE / (pow(2.0, num_of_quad_divisions));
		GLfloat node_y_width = COORD_SIZE / (pow(2.0, num_of_quad_divisions));

		// loop to find the coordinates where our quadtree begins
		gboolean ignore_last = FALSE;
		guint i, loops = (node->port.network_bits + 1) / 2;
		GLfloat adjustment = COORD_SIZE;
		for (i = 0; i < loops; ++i) {
			adjustment /= 2.0;

			// if its our last loop and we have an odd num of network bits, ignore the last bit and double our y_width
			if ((i == (loops - 1)) && (node->port.network_bits % 2 == 1)) {
				ignore_last = TRUE;
				node_y_width *= 2.0;
			}

			// for a COORD_SIZE x COORD_SIZE grid
			switch (SHIFT(node->port.addr.s_addr, ((((i/4)+1)*8) - 2*((i%4)+1)), 0x3)) {
				// 00
				case 0:
					// do nothing
				break;

				// 01
				case 1:
					if (!ignore_last) {
						node->port.coords.y_coord += adjustment;
					}
				break;

				// 10
				case 2:
					node->port.coords.x_coord += adjustment;
				break;

				// 11
				case 3:
					node->port.coords.x_coord += adjustment;

					if (!ignore_last) {
						node->port.coords.y_coord += adjustment;
					}
				break;
			}
		}


		GLfloat unit_size;
		bound_t *bound;

		if (viz->mode.data_type == SRC_PORT) {
			bound = &viz->src_bound;
			unit_size = CUBE_SIZE  / (GLfloat) (viz->src_bound.max_x_coord - viz->src_bound.min_x_coord);
		} else if (viz->mode.data_type == DST_PORT) {
			bound = &viz->dst_bound;
			unit_size = CUBE_SIZE  / (GLfloat) (viz->dst_bound.max_x_coord - viz->dst_bound.min_x_coord);
		} else {
			return;
		}

		// adjust to the bounded box coords
		node->port.coords.x_coord -= bound->min_x_coord;
		node->port.coords.y_coord -= bound->min_y_coord;

		// translate coords and dimensions onto -1 through 1 coord system
		node->port.coords.x_coord = (GLfloat) (node->port.coords.x_coord * unit_size) - 1.0;
		node->port.coords.y_coord = (GLfloat) (node->port.coords.y_coord * unit_size) - 1.0;
		node->port.coords.x_width = (GLfloat) (node_x_width * unit_size);
		node->port.coords.y_width = (GLfloat) (node_y_width * unit_size);

		// add the node to our GTree
		g_tree_insert(tree, node, NULL);
	}
}


gchar *handler_port_get_node_description(tree_node_t *node)
{
	gchar *port_desc, *as_desc, *full_desc;

	if (node->port.port_num >= viz->port_numbers->len) {
		port_desc = "Private Port";
	} else {
		port_desc = g_ptr_array_index(viz->port_numbers, node->port.port_num);

		if (port_desc == NULL) {
			port_desc = "Unknown Port";
		}
	}

	if (node->port.origin_as >= viz->asnames->len) {
		as_desc = "Unknown AS";
	} else {
		as_desc = g_ptr_array_index(viz->asnames, node->port.origin_as);

		if (as_desc == NULL) {
			as_desc = "Unknown AS";
		}
	}

	full_desc = g_strdup_printf("Port #%d (%s)  -  AS #%d (%s)", node->port.port_num, port_desc, node->port.origin_as, as_desc);

	return (full_desc);
}


gchar *handler_port_get_node_title(void)
{
	return (g_strdup("Address:Port"));
}


gchar *handler_port_get_node_value(tree_node_t *node)
{
	return (g_strdup_printf("%s:%d", inet_ntoa(node->port.addr), node->port.port_num));
}
