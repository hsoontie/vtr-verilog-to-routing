/* 
 * Prepacking: Group together technology-mapped netlist blocks before packing.  
 * This gives hints to the packer on what groups of blocks to keep together during packing.
 * Primary purpose:
 *    1) "Forced" packs (eg LUT+FF pair)
 *    2) Carry-chains
 * Duties: Find pack patterns in architecture, find pack patterns in netlist.
 *
 * Author: Jason Luu
 * March 12, 2012
 */

#include <cstdio>
#include <cstring>
#include <map>
using namespace std;

#include "vtr_util.h"
#include "vtr_assert.h"

#include "vpr_types.h"
#include "vpr_error.h"

#include "read_xml_arch_file.h"
#include "globals.h"
#include "atom_netlist.h"
#include "hash.h"
#include "prepack.h"
#include "vpr_utils.h"
#include "ReadOptions.h"

/*****************************************/
/*Local Function Declaration			 */
/*****************************************/
static int add_pattern_name_to_hash(struct s_hash **nhash,
		const char *pattern_name, int *ncount);
static void discover_pattern_names_in_pb_graph_node(
		t_pb_graph_node *pb_graph_node, struct s_hash **nhash,
		int *ncount);
static void forward_infer_pattern(t_pb_graph_pin *pb_graph_pin);
static void backward_infer_pattern(t_pb_graph_pin *pb_graph_pin);
static t_pack_patterns *alloc_and_init_pattern_list_from_hash(const int ncount,
		struct s_hash **nhash);
static t_pb_graph_edge * find_expansion_edge_of_pattern(const int pattern_index,
		const t_pb_graph_node *pb_graph_node);
static void forward_expand_pack_pattern_from_edge(
		const t_pb_graph_edge *expansion_edge,
		t_pack_patterns *list_of_packing_patterns,
		const int curr_pattern_index, int *L_num_blocks, const bool make_root_of_chain);
static void backward_expand_pack_pattern_from_edge(
		const t_pb_graph_edge* expansion_edge,
		t_pack_patterns *list_of_packing_patterns,
		const int curr_pattern_index, t_pb_graph_pin *destination_pin,
		t_pack_pattern_block *destination_block, int *L_num_blocks);
static int compare_pack_pattern(const t_pack_patterns *pattern_a, const t_pack_patterns *pattern_b);
static void free_pack_pattern(t_pack_pattern_block *pattern_block, t_pack_pattern_block **pattern_block_list);
static t_pack_molecule *try_create_molecule(
		t_pack_patterns *list_of_pack_patterns, 
        std::multimap<AtomBlockId,t_pack_molecule*>& atom_molecules,
        const int pack_pattern_index,
		AtomBlockId blk_id);
static bool try_expand_molecule(t_pack_molecule *molecule,
        const std::multimap<AtomBlockId,t_pack_molecule*>& atom_molecules,
		const AtomBlockId blk_id,
		const t_pack_pattern_block *current_pattern_block);
static void print_pack_molecules(const char *fname,
		const t_pack_patterns *list_of_pack_patterns, const int num_pack_patterns,
		const t_pack_molecule *list_of_molecules);
static t_pb_graph_node* get_expected_lowest_cost_primitive_for_atom_block(const AtomBlockId blk_id);
static t_pb_graph_node* get_expected_lowest_cost_primitive_for_atom_block_in_pb_graph_node(const AtomBlockId blk_id, t_pb_graph_node *curr_pb_graph_node, float *cost);
static AtomBlockId find_new_root_atom_for_chain(const AtomBlockId blk_id, const t_pack_patterns *list_of_pack_pattern, 
        const std::multimap<AtomBlockId,t_pack_molecule*>& atom_molecules);

/*****************************************/
/*Function Definitions					 */
/*****************************************/

/**
 * Find all packing patterns in architecture 
 * [0..num_packing_patterns-1]
 *
 * Limitations: Currently assumes that forced pack nets must be single-fanout
 * as this covers all the reasonable architectures we wanted.
 * More complicated structures should probably be handled either downstream 
 * (general packing) or upstream (in tech mapping).
 * If this limitation is too constraining, code is designed so that this limitation can be removed.
 */
t_pack_patterns *alloc_and_load_pack_patterns(int *num_packing_patterns) {
	int i, j, ncount, k;
	int L_num_blocks;
	struct s_hash **nhash;
	t_pack_patterns *list_of_packing_patterns;
	t_pb_graph_edge *expansion_edge;

	/* alloc and initialize array of packing patterns based on architecture complex blocks */
	nhash = alloc_hash_table();
	ncount = 0;
	for (i = 0; i < num_types; i++) {
		discover_pattern_names_in_pb_graph_node(
				type_descriptors[i].pb_graph_head, nhash, &ncount);
	}

	list_of_packing_patterns = alloc_and_init_pattern_list_from_hash(ncount,
			nhash);

	/* load packing patterns by traversing the edges to find edges belonging to pattern */
	for (i = 0; i < ncount; i++) {
		for (j = 0; j < num_types; j++) {
			expansion_edge = find_expansion_edge_of_pattern(i,
					type_descriptors[j].pb_graph_head);
			if (expansion_edge == NULL) {
				continue;
			}
			L_num_blocks = 0;
			list_of_packing_patterns[i].base_cost = 0;
			backward_expand_pack_pattern_from_edge(expansion_edge,
					list_of_packing_patterns, i, NULL, NULL, &L_num_blocks);
			list_of_packing_patterns[i].num_blocks = L_num_blocks;

			/* Default settings: A section of a netlist must match all blocks in a pack 
             * pattern before it can be made a molecule except for carry-chains.  
             * For carry-chains, since carry-chains are typically quite flexible in terms 
             * of size, it is optional whether or not an atom in a netlist matches any 
             * particular block inside the chain */
			list_of_packing_patterns[i].is_block_optional = (bool*) vtr::malloc(L_num_blocks * sizeof(bool));
			for(k = 0; k < L_num_blocks; k++) {
				list_of_packing_patterns[i].is_block_optional[k] = false;
				if(list_of_packing_patterns[i].is_chain && list_of_packing_patterns[i].root_block->block_id != k) {
					list_of_packing_patterns[i].is_block_optional[k] = true;
				}
			}
			break;
		}
	}

	free_hash_table(nhash);

	*num_packing_patterns = ncount;

	return list_of_packing_patterns;
}

/**
 * Adds pack pattern name to hashtable of pack pattern names.
 */
static int add_pattern_name_to_hash(struct s_hash **nhash,
		const char *pattern_name, int *ncount) {
	struct s_hash *hash_value;

	hash_value = insert_in_hash_table(nhash, pattern_name, *ncount);
	if (hash_value->count == 1) {
		VTR_ASSERT(*ncount == hash_value->index);
		(*ncount)++;
	}
	return hash_value->index;
}

/**
 * Locate all pattern names 
 * Side-effect: set all pb_graph_node temp_scratch_pad field to NULL
 *				For cases where a pattern inference is "obvious", mark it as obvious.
 */
static void discover_pattern_names_in_pb_graph_node(
		t_pb_graph_node *pb_graph_node, struct s_hash **nhash,
		int *ncount) {
	int i, j, k, m;
	int index;
	bool hasPattern;
	/* Iterate over all edges to discover if an edge in current physical block belongs to a pattern 
	 If edge does, then record the name of the pattern in a hash table
	 */

	if (pb_graph_node == NULL) {
		return;
	}

	pb_graph_node->temp_scratch_pad = NULL;

	for (i = 0; i < pb_graph_node->num_input_ports; i++) {
		for (j = 0; j < pb_graph_node->num_input_pins[i]; j++) {
			hasPattern = false;
			for (k = 0; k < pb_graph_node->input_pins[i][j].num_output_edges;
					k++) {
				for (m = 0;
						m
								< pb_graph_node->input_pins[i][j].output_edges[k]->num_pack_patterns;
						m++) {
					hasPattern = true;
					index =
							add_pattern_name_to_hash(nhash,
									pb_graph_node->input_pins[i][j].output_edges[k]->pack_pattern_names[m],
									ncount);
					if (pb_graph_node->input_pins[i][j].output_edges[k]->pack_pattern_indices
							== NULL) {
						pb_graph_node->input_pins[i][j].output_edges[k]->pack_pattern_indices = (int*)
								vtr::malloc(
										pb_graph_node->input_pins[i][j].output_edges[k]->num_pack_patterns
												* sizeof(int));
					}
					pb_graph_node->input_pins[i][j].output_edges[k]->pack_pattern_indices[m] =
							index;
				}								
			}
			if (hasPattern == true) {
				forward_infer_pattern(&pb_graph_node->input_pins[i][j]);
				backward_infer_pattern(&pb_graph_node->input_pins[i][j]);
			}
		}
	}

	for (i = 0; i < pb_graph_node->num_output_ports; i++) {
		for (j = 0; j < pb_graph_node->num_output_pins[i]; j++) {
			hasPattern = false;
			for (k = 0; k < pb_graph_node->output_pins[i][j].num_output_edges;
					k++) {
				for (m = 0;
						m
								< pb_graph_node->output_pins[i][j].output_edges[k]->num_pack_patterns;
						m++) {
					hasPattern = true;
					index =
							add_pattern_name_to_hash(nhash,
									pb_graph_node->output_pins[i][j].output_edges[k]->pack_pattern_names[m],
									ncount);
					if (pb_graph_node->output_pins[i][j].output_edges[k]->pack_pattern_indices
							== NULL) {
						pb_graph_node->output_pins[i][j].output_edges[k]->pack_pattern_indices = (int*)
								vtr::malloc(
										pb_graph_node->output_pins[i][j].output_edges[k]->num_pack_patterns
												* sizeof(int));
					}
					pb_graph_node->output_pins[i][j].output_edges[k]->pack_pattern_indices[m] =
							index;
				}
			}
			if (hasPattern == true) {
				forward_infer_pattern(&pb_graph_node->output_pins[i][j]);
				backward_infer_pattern(&pb_graph_node->output_pins[i][j]);
			}
		}
	}

	for (i = 0; i < pb_graph_node->num_clock_ports; i++) {
		for (j = 0; j < pb_graph_node->num_clock_pins[i]; j++) {
			hasPattern = false;
			for (k = 0; k < pb_graph_node->clock_pins[i][j].num_output_edges;
					k++) {
				for (m = 0;
						m
								< pb_graph_node->clock_pins[i][j].output_edges[k]->num_pack_patterns;
						m++) {
					hasPattern = true;
					index =
							add_pattern_name_to_hash(nhash,
									pb_graph_node->clock_pins[i][j].output_edges[k]->pack_pattern_names[m],
									ncount);
					if (pb_graph_node->clock_pins[i][j].output_edges[k]->pack_pattern_indices
							== NULL) {
						pb_graph_node->clock_pins[i][j].output_edges[k]->pack_pattern_indices = (int*)
								vtr::malloc(
										pb_graph_node->clock_pins[i][j].output_edges[k]->num_pack_patterns
												* sizeof(int));
					}
					pb_graph_node->clock_pins[i][j].output_edges[k]->pack_pattern_indices[m] =
							index;
				}
			}
			if (hasPattern == true) {
				forward_infer_pattern(&pb_graph_node->clock_pins[i][j]);
				backward_infer_pattern(&pb_graph_node->clock_pins[i][j]);
			}
		}
	}

	for (i = 0; i < pb_graph_node->pb_type->num_modes; i++) {
		for (j = 0; j < pb_graph_node->pb_type->modes[i].num_pb_type_children;
				j++) {
			for (k = 0;
					k
							< pb_graph_node->pb_type->modes[i].pb_type_children[j].num_pb;
					k++) {
				discover_pattern_names_in_pb_graph_node(
						&pb_graph_node->child_pb_graph_nodes[i][j][k], nhash,
						ncount);
			}
		}
	}
}

/**
 * In obvious cases where a pattern edge has only one path to go, set that path to be inferred 
 */
static void forward_infer_pattern(t_pb_graph_pin *pb_graph_pin) {
	if (pb_graph_pin->num_output_edges == 1 && pb_graph_pin->output_edges[0]->num_pack_patterns == 0 && pb_graph_pin->output_edges[0]->infer_pattern == false) {
		pb_graph_pin->output_edges[0]->infer_pattern = true;
		if (pb_graph_pin->output_edges[0]->num_output_pins == 1) {
			forward_infer_pattern(pb_graph_pin->output_edges[0]->output_pins[0]);
		}
	}
}
static void backward_infer_pattern(t_pb_graph_pin *pb_graph_pin) {
	if (pb_graph_pin->num_input_edges == 1 && pb_graph_pin->input_edges[0]->num_pack_patterns == 0 && pb_graph_pin->input_edges[0]->infer_pattern == false) {
		pb_graph_pin->input_edges[0]->infer_pattern = true;
		if (pb_graph_pin->input_edges[0]->num_input_pins == 1) {
			backward_infer_pattern(pb_graph_pin->input_edges[0]->input_pins[0]);
		}
	}
}

/**
 * Allocates memory for models and loads the name of the packing pattern 
 * so that it can be identified and loaded with more complete information later
 */
static t_pack_patterns *alloc_and_init_pattern_list_from_hash(const int ncount,
		struct s_hash **nhash) {
	t_pack_patterns *nlist;
	struct s_hash_iterator hash_iter;
	struct s_hash *curr_pattern;

	nlist = (t_pack_patterns*)vtr::calloc(ncount, sizeof(t_pack_patterns));

	hash_iter = start_hash_table_iterator();
	curr_pattern = get_next_hash(nhash, &hash_iter);
	while (curr_pattern != NULL) {
		VTR_ASSERT(nlist[curr_pattern->index].name == NULL);
		nlist[curr_pattern->index].name = vtr::strdup(curr_pattern->name);
		nlist[curr_pattern->index].root_block = NULL;
		nlist[curr_pattern->index].is_chain = false;
		nlist[curr_pattern->index].index = curr_pattern->index;
		curr_pattern = get_next_hash(nhash, &hash_iter);
	}
	return nlist;
}

void free_list_of_pack_patterns(t_pack_patterns *list_of_pack_patterns, const int num_packing_patterns) {
	int i, j, num_pack_pattern_blocks;
	t_pack_pattern_block **pattern_block_list;
	if (list_of_pack_patterns != NULL) {
		for (i = 0; i < num_packing_patterns; i++) {
			num_pack_pattern_blocks = list_of_pack_patterns[i].num_blocks;
			pattern_block_list = (t_pack_pattern_block **)vtr::calloc(num_pack_pattern_blocks, sizeof(t_pack_pattern_block *));
			free(list_of_pack_patterns[i].name);
			free(list_of_pack_patterns[i].is_block_optional);
			free_pack_pattern(list_of_pack_patterns[i].root_block, pattern_block_list);
			for (j = 0; j < num_pack_pattern_blocks; j++) {
				free(pattern_block_list[j]);
			}
			free(pattern_block_list);
		}
		free(list_of_pack_patterns);
	}
}

/**
 * Locate first edge that belongs to pattern index 
 */
static t_pb_graph_edge * find_expansion_edge_of_pattern(const int pattern_index,
		const t_pb_graph_node *pb_graph_node) {
	int i, j, k, m;
	t_pb_graph_edge * edge;
	/* Iterate over all edges to discover if an edge in current physical block belongs to a pattern 
	 If edge does, then return that edge
	 */

	if (pb_graph_node == NULL) {
		return NULL;
	}

	for (i = 0; i < pb_graph_node->num_input_ports; i++) {
		for (j = 0; j < pb_graph_node->num_input_pins[i]; j++) {
			for (k = 0; k < pb_graph_node->input_pins[i][j].num_output_edges;
					k++) {
				for (m = 0;
						m
								< pb_graph_node->input_pins[i][j].output_edges[k]->num_pack_patterns;
						m++) {
					if (pb_graph_node->input_pins[i][j].output_edges[k]->pack_pattern_indices[m]
							== pattern_index) {
						return pb_graph_node->input_pins[i][j].output_edges[k];
					}
				}
			}
		}
	}

	for (i = 0; i < pb_graph_node->num_output_ports; i++) {
		for (j = 0; j < pb_graph_node->num_output_pins[i]; j++) {
			for (k = 0; k < pb_graph_node->output_pins[i][j].num_output_edges;
					k++) {
				for (m = 0;
						m
								< pb_graph_node->output_pins[i][j].output_edges[k]->num_pack_patterns;
						m++) {
					if (pb_graph_node->output_pins[i][j].output_edges[k]->pack_pattern_indices[m]
							== pattern_index) {
						return pb_graph_node->output_pins[i][j].output_edges[k];
					}
				}
			}
		}
	}

	for (i = 0; i < pb_graph_node->num_clock_ports; i++) {
		for (j = 0; j < pb_graph_node->num_clock_pins[i]; j++) {
			for (k = 0; k < pb_graph_node->clock_pins[i][j].num_output_edges;
					k++) {
				for (m = 0;
						m
								< pb_graph_node->clock_pins[i][j].output_edges[k]->num_pack_patterns;
						m++) {
					if (pb_graph_node->clock_pins[i][j].output_edges[k]->pack_pattern_indices[m]
							== pattern_index) {
						return pb_graph_node->clock_pins[i][j].output_edges[k];
					}
				}
			}
		}
	}

	for (i = 0; i < pb_graph_node->pb_type->num_modes; i++) {
		for (j = 0; j < pb_graph_node->pb_type->modes[i].num_pb_type_children;
				j++) {
			for (k = 0;
					k
							< pb_graph_node->pb_type->modes[i].pb_type_children[j].num_pb;
					k++) {
				edge = find_expansion_edge_of_pattern(pattern_index,
						&pb_graph_node->child_pb_graph_nodes[i][j][k]);
				if (edge != NULL) {
					return edge;
				}
			}
		}
	}
	return NULL;
}

/** 
 * Find if receiver of edge is in the same pattern, if yes, add to pattern
 *  Convention: Connections are made on backward expansion only (to make future 
 *              multi-fanout support easier) so this function will not update connections
 */
static void forward_expand_pack_pattern_from_edge(
		const t_pb_graph_edge* expansion_edge,
		t_pack_patterns *list_of_packing_patterns,
		const int curr_pattern_index, int *L_num_blocks, bool make_root_of_chain) {
	int i, j, k;
	int iport, ipin, iedge;
	bool found; /* Error checking, ensure only one fan-out for each pattern net */
	t_pack_pattern_block *destination_block = NULL;
	t_pb_graph_node *destination_pb_graph_node = NULL;

	found = expansion_edge->infer_pattern;
	for (i = 0;	!found && i < expansion_edge->num_pack_patterns; i++) {
		if (expansion_edge->pack_pattern_indices[i] == curr_pattern_index) {
			found = true;
		}
	}
	if (!found) {
		return;
	}

	found = false;
	for (i = 0; i < expansion_edge->num_output_pins; i++) {
		if (expansion_edge->output_pins[i]->parent_node->pb_type->num_modes
				== 0) {
			destination_pb_graph_node =
					expansion_edge->output_pins[i]->parent_node;
			VTR_ASSERT(found == false);
			/* Check assumption that each forced net has only one fan-out */
			/* This is the destination node */
			found = true;

			/* If this pb_graph_node is part not of the current pattern index, put it in and expand all its edges */
			if (destination_pb_graph_node->temp_scratch_pad == NULL
					|| ((t_pack_pattern_block*) destination_pb_graph_node->temp_scratch_pad)->pattern_index
							!= curr_pattern_index) {
				destination_block = (t_pack_pattern_block*)vtr::calloc(1, sizeof(t_pack_pattern_block));
				list_of_packing_patterns[curr_pattern_index].base_cost +=
						compute_primitive_base_cost(destination_pb_graph_node);
				destination_block->block_id = *L_num_blocks;
				(*L_num_blocks)++;
				destination_pb_graph_node->temp_scratch_pad =
						(void *) destination_block;
				destination_block->pattern_index = curr_pattern_index;
				destination_block->pb_type = destination_pb_graph_node->pb_type;
				for (iport = 0;
						iport < destination_pb_graph_node->num_input_ports;
						iport++) {
					for (ipin = 0;
							ipin
									< destination_pb_graph_node->num_input_pins[iport];
							ipin++) {
						for (iedge = 0;
								iedge
										< destination_pb_graph_node->input_pins[iport][ipin].num_input_edges;
								iedge++) {
							backward_expand_pack_pattern_from_edge(
									destination_pb_graph_node->input_pins[iport][ipin].input_edges[iedge],
									list_of_packing_patterns,
									curr_pattern_index,
									&destination_pb_graph_node->input_pins[iport][ipin],
									destination_block, L_num_blocks);
						}
					}
				}
				for (iport = 0;
						iport < destination_pb_graph_node->num_output_ports;
						iport++) {
					for (ipin = 0;
							ipin
									< destination_pb_graph_node->num_output_pins[iport];
							ipin++) {
						for (iedge = 0;
								iedge
										< destination_pb_graph_node->output_pins[iport][ipin].num_output_edges;
								iedge++) {
							forward_expand_pack_pattern_from_edge(
									destination_pb_graph_node->output_pins[iport][ipin].output_edges[iedge],
									list_of_packing_patterns,
									curr_pattern_index, L_num_blocks, false);
						}
					}
				}
				for (iport = 0;
						iport < destination_pb_graph_node->num_clock_ports;
						iport++) {
					for (ipin = 0;
							ipin
									< destination_pb_graph_node->num_clock_pins[iport];
							ipin++) {
						for (iedge = 0;
								iedge
										< destination_pb_graph_node->clock_pins[iport][ipin].num_input_edges;
								iedge++) {
							backward_expand_pack_pattern_from_edge(
									destination_pb_graph_node->clock_pins[iport][ipin].input_edges[iedge],
									list_of_packing_patterns,
									curr_pattern_index,
									&destination_pb_graph_node->clock_pins[iport][ipin],
									destination_block, L_num_blocks);
						}
					}
				}
			} 
			if (((t_pack_pattern_block*) destination_pb_graph_node->temp_scratch_pad)->pattern_index
							== curr_pattern_index) {
				if(make_root_of_chain == true) {
					list_of_packing_patterns[curr_pattern_index].chain_root_pin = expansion_edge->output_pins[i];
					list_of_packing_patterns[curr_pattern_index].root_block = destination_block;
				}
			}
		} else {
			for (j = 0; j < expansion_edge->output_pins[i]->num_output_edges;
					j++) {
				if (expansion_edge->output_pins[i]->output_edges[j]->infer_pattern
						== true) {
					forward_expand_pack_pattern_from_edge(
							expansion_edge->output_pins[i]->output_edges[j],
							list_of_packing_patterns, curr_pattern_index,
							L_num_blocks, make_root_of_chain);
				} else {
					for (k = 0;
							k
									< expansion_edge->output_pins[i]->output_edges[j]->num_pack_patterns;
							k++) {
						if (expansion_edge->output_pins[i]->output_edges[j]->pack_pattern_indices[k]
								== curr_pattern_index) {
							if (found == true) {
								/* Check assumption that each forced net has only one fan-out */
								vpr_throw(VPR_ERROR_PACK, __FILE__, __LINE__, 
										"Invalid packing pattern defined.  Multi-fanout nets not supported when specifying pack patterns.\n"
										"Problem on %s[%d].%s[%d]\n",
										expansion_edge->output_pins[i]->parent_node->pb_type->name,
										expansion_edge->output_pins[i]->parent_node->placement_index,
										expansion_edge->output_pins[i]->port->name,
										expansion_edge->output_pins[i]->pin_number
										);
							}
							found = true;
							forward_expand_pack_pattern_from_edge(
									expansion_edge->output_pins[i]->output_edges[j],
									list_of_packing_patterns,
									curr_pattern_index, L_num_blocks, make_root_of_chain);
						}
					}
				}
			}
		}
	}

}

/** 
 * Find if driver of edge is in the same pattern, if yes, add to pattern
 *  Convention: Connections are made on backward expansion only (to make future multi-
 *               fanout support easier) so this function must update both source and 
 *               destination blocks
 */
static void backward_expand_pack_pattern_from_edge(
		const t_pb_graph_edge* expansion_edge,
		t_pack_patterns *list_of_packing_patterns,
		const int curr_pattern_index, t_pb_graph_pin *destination_pin,
		t_pack_pattern_block *destination_block, int *L_num_blocks) {
	int i, j, k;
	int iport, ipin, iedge;
	bool found; /* Error checking, ensure only one fan-out for each pattern net */
	t_pack_pattern_block *source_block = NULL;
	t_pb_graph_node *source_pb_graph_node = NULL;
	t_pack_pattern_connections *pack_pattern_connection = NULL;

	found = expansion_edge->infer_pattern;
	for (i = 0;	!found && i < expansion_edge->num_pack_patterns; i++) {
		if (expansion_edge->pack_pattern_indices[i] == curr_pattern_index) {
			found = true;
		}
	}
	if (!found) {
		return;
	}

	found = false;
	for (i = 0; i < expansion_edge->num_input_pins; i++) {
		if (expansion_edge->input_pins[i]->parent_node->pb_type->num_modes
				== 0) {
			source_pb_graph_node = expansion_edge->input_pins[i]->parent_node;
			VTR_ASSERT(found == false);
			/* Check assumption that each forced net has only one fan-out */
			/* This is the source node for destination */
			found = true;

			/* If this pb_graph_node is part not of the current pattern index, put it in and expand all its edges */
			source_block =
					(t_pack_pattern_block*) source_pb_graph_node->temp_scratch_pad;
			if (source_block == NULL
					|| source_block->pattern_index != curr_pattern_index) {
				source_block = (t_pack_pattern_block *)vtr::calloc(1, sizeof(t_pack_pattern_block));
				source_block->block_id = *L_num_blocks;
				(*L_num_blocks)++;
				list_of_packing_patterns[curr_pattern_index].base_cost +=
						compute_primitive_base_cost(source_pb_graph_node);
				source_pb_graph_node->temp_scratch_pad = (void *) source_block;
				source_block->pattern_index = curr_pattern_index;
				source_block->pb_type = source_pb_graph_node->pb_type;

				if (list_of_packing_patterns[curr_pattern_index].root_block
						== NULL) {
					list_of_packing_patterns[curr_pattern_index].root_block =
							source_block;
				}

				for (iport = 0; iport < source_pb_graph_node->num_input_ports;
						iport++) {
					for (ipin = 0;
							ipin < source_pb_graph_node->num_input_pins[iport];
							ipin++) {
						for (iedge = 0;
								iedge
										< source_pb_graph_node->input_pins[iport][ipin].num_input_edges;
								iedge++) {
							backward_expand_pack_pattern_from_edge(
									source_pb_graph_node->input_pins[iport][ipin].input_edges[iedge],
									list_of_packing_patterns,
									curr_pattern_index,
									&source_pb_graph_node->input_pins[iport][ipin],
									source_block, L_num_blocks);
						}
					}
				}
				for (iport = 0; iport < source_pb_graph_node->num_output_ports;
						iport++) {
					for (ipin = 0;
							ipin < source_pb_graph_node->num_output_pins[iport];
							ipin++) {
						for (iedge = 0;
								iedge
										< source_pb_graph_node->output_pins[iport][ipin].num_output_edges;
								iedge++) {
							forward_expand_pack_pattern_from_edge(
									source_pb_graph_node->output_pins[iport][ipin].output_edges[iedge],
									list_of_packing_patterns,
									curr_pattern_index, L_num_blocks, false);
						}
					}
				}
				for (iport = 0; iport < source_pb_graph_node->num_clock_ports;
						iport++) {
					for (ipin = 0;
							ipin < source_pb_graph_node->num_clock_pins[iport];
							ipin++) {
						for (iedge = 0;
								iedge
										< source_pb_graph_node->clock_pins[iport][ipin].num_input_edges;
								iedge++) {
							backward_expand_pack_pattern_from_edge(
									source_pb_graph_node->clock_pins[iport][ipin].input_edges[iedge],
									list_of_packing_patterns,
									curr_pattern_index,
									&source_pb_graph_node->clock_pins[iport][ipin],
									source_block, L_num_blocks);
						}
					}
				}
			}
			if (destination_pin != NULL) {
				VTR_ASSERT(
						((t_pack_pattern_block*)source_pb_graph_node->temp_scratch_pad)->pattern_index == curr_pattern_index);
				source_block =
						(t_pack_pattern_block*) source_pb_graph_node->temp_scratch_pad;
				pack_pattern_connection = (t_pack_pattern_connections *)vtr::calloc(1,
						sizeof(t_pack_pattern_connections));
				pack_pattern_connection->from_block = source_block;
				pack_pattern_connection->from_pin =
						expansion_edge->input_pins[i];
				pack_pattern_connection->to_block = destination_block;
				pack_pattern_connection->to_pin = destination_pin;
				pack_pattern_connection->next = source_block->connections;
				source_block->connections = pack_pattern_connection;

				pack_pattern_connection = (t_pack_pattern_connections *)vtr::calloc(1,
						sizeof(t_pack_pattern_connections));
				pack_pattern_connection->from_block = source_block;
				pack_pattern_connection->from_pin =
						expansion_edge->input_pins[i];
				pack_pattern_connection->to_block = destination_block;
				pack_pattern_connection->to_pin = destination_pin;
				pack_pattern_connection->next = destination_block->connections;
				destination_block->connections = pack_pattern_connection;

				if (source_block == destination_block) {
					vpr_throw(VPR_ERROR_PACK, __FILE__, __LINE__, 
							"Invalid packing pattern defined. Source and destination block are the same (%s).\n",
							source_block->pb_type->name);
				}
			}
		} else {
			if(expansion_edge->input_pins[i]->num_input_edges == 0) {
				if(expansion_edge->input_pins[i]->parent_node->pb_type->parent_mode == NULL) {
					/* This pack pattern extends to CLB input pin, thus it extends across multiple logic blocks, treat as a chain */
					list_of_packing_patterns[curr_pattern_index].is_chain = true;
					forward_expand_pack_pattern_from_edge(
									expansion_edge,
									list_of_packing_patterns,
									curr_pattern_index, L_num_blocks, true);
				}
			} else {
				for (j = 0; j < expansion_edge->input_pins[i]->num_input_edges;
						j++) {
					if (expansion_edge->input_pins[i]->input_edges[j]->infer_pattern
							== true) {
						backward_expand_pack_pattern_from_edge(
								expansion_edge->input_pins[i]->input_edges[j],
								list_of_packing_patterns, curr_pattern_index,
								destination_pin, destination_block, L_num_blocks);
					} else {
						for (k = 0;
								k
										< expansion_edge->input_pins[i]->input_edges[j]->num_pack_patterns;
								k++) {
							if (expansion_edge->input_pins[i]->input_edges[j]->pack_pattern_indices[k]
									== curr_pattern_index) {
								VTR_ASSERT(found == false);
								/* Check assumption that each forced net has only one fan-out */
								found = true;
								backward_expand_pack_pattern_from_edge(
										expansion_edge->input_pins[i]->input_edges[j],
										list_of_packing_patterns,
										curr_pattern_index, destination_pin,
										destination_block, L_num_blocks);
							}
						}
					}
				}
			}
		}
	}
}

/**
 * Pre-pack atoms in netlist to molecules
 * 1.  Single atoms are by definition a molecule.
 * 2.  Forced pack molecules are groupings of atoms that matches a t_pack_pattern definition.
 * 3.  Chained molecules are molecules that follow a carry-chain style pattern,
 *     ie. a single linear chain that can be split across multiple complex blocks
 */
t_pack_molecule *alloc_and_load_pack_molecules(
		t_pack_patterns *list_of_pack_patterns,
        std::multimap<AtomBlockId,t_pack_molecule*>& atom_molecules,
        std::unordered_map<AtomBlockId,t_pb_graph_node*>& expected_lowest_cost_pb_gnode,
		const int num_packing_patterns) {
	int i, j, best_pattern;
	t_pack_molecule *list_of_molecules_head;
	t_pack_molecule *cur_molecule;
	bool *is_used;

	is_used = (bool*)vtr::calloc(num_packing_patterns, sizeof(bool));

	cur_molecule = list_of_molecules_head = NULL;

	/* Find forced pack patterns
	 * Simplifying assumptions: Each atom can map to at most one molecule, 
     *                          use first-fit mapping based on priority of pattern
	 * TODO: Need to investigate better mapping strategies than first-fit 
     */
	for (i = 0; i < num_packing_patterns; i++) {
		best_pattern = 0;
		for(j = 1; j < num_packing_patterns; j++) {
			if(is_used[best_pattern]) {
				best_pattern = j;
			} else if (is_used[j] == false && compare_pack_pattern(&list_of_pack_patterns[j], &list_of_pack_patterns[best_pattern]) == 1) {
				best_pattern = j;
			}
		}
		VTR_ASSERT(is_used[best_pattern] == false);
		is_used[best_pattern] = true;
        for(auto blk_id : g_atom_nl.blocks()) {

            bool retry = false;
            do {
                cur_molecule = try_create_molecule(list_of_pack_patterns, atom_molecules, best_pattern, blk_id);
                if (cur_molecule != NULL) {
                    cur_molecule->next = list_of_molecules_head;
                    /* In the event of multiple molecules with the same atom block pattern, 
                     * bias to use the molecule with less costly physical resources first */
                    /* TODO: Need to normalize magical number 100 */
                    cur_molecule->base_gain = cur_molecule->num_blocks - (cur_molecule->pack_pattern->base_cost / 100);
                    list_of_molecules_head = cur_molecule;

                    //Note: atom_molecules is an (ordered) multimap so the last molecule 
                    //      inserted for a given blk_id will be the last valid element 
                    //      in the equal_range
                    auto rng = atom_molecules.equal_range(blk_id); //The range of molecules matching this block
                    bool range_empty = (rng.first == rng.second);
                    auto last_valid_iter = --rng.second; //Iterator to last element
                    bool cur_was_last_inserted = (last_valid_iter->second == cur_molecule);
                    if(range_empty || !cur_was_last_inserted) {
                        /* molecule did not cover current atom (possibly because molecule created is
                         * part of a long chain that extends past multiple logic blocks), try again */
                        retry = true;
                    }
                }
            } while(retry);
		}
	}
	free(is_used);

	/* List all atom blocks as a molecule for blocks that do not belong to any molecules.
	 This allows the packer to be consistent as it now packs molecules only instead of atoms and molecules

	 If a block belongs to a molecule, then carrying the single atoms around can make the packing problem
	 more difficult because now it needs to consider splitting molecules.
	 */
    for(auto blk_id : g_atom_nl.blocks()) {

        expected_lowest_cost_pb_gnode[blk_id] = get_expected_lowest_cost_primitive_for_atom_block(blk_id);

        auto rng = atom_molecules.equal_range(blk_id);
        bool rng_empty = (rng.first == rng.second);
		if (rng_empty) {
			cur_molecule = (t_pack_molecule*) vtr::calloc(1, sizeof(t_pack_molecule));
			cur_molecule->valid = true;
			cur_molecule->type = MOLECULE_SINGLE_ATOM;
			cur_molecule->num_blocks = 1;
			cur_molecule->root = 0;
			cur_molecule->num_ext_inputs = num_used_pins(g_atom_nl, g_atom_nl.block_input_ports(blk_id));
			cur_molecule->chain_pattern = NULL;
			cur_molecule->pack_pattern = NULL;

            cur_molecule->atom_block_ids = {blk_id};

			cur_molecule->next = list_of_molecules_head;
			cur_molecule->base_gain = 1;
			list_of_molecules_head = cur_molecule;

            atom_molecules.insert({blk_id, cur_molecule});
		}
	}

	if (getEchoEnabled() && isEchoFileEnabled(E_ECHO_PRE_PACKING_MOLECULES_AND_PATTERNS)) {
		print_pack_molecules(getEchoFileName(E_ECHO_PRE_PACKING_MOLECULES_AND_PATTERNS),
				list_of_pack_patterns, num_packing_patterns,
				list_of_molecules_head);
	}

	return list_of_molecules_head;
}


static void free_pack_pattern(t_pack_pattern_block *pattern_block, t_pack_pattern_block **pattern_block_list) {
	t_pack_pattern_connections *connection, *next;
	if (pattern_block == NULL || pattern_block->block_id == OPEN) {
		/* already traversed, return */
		return; 
	}
	pattern_block_list[pattern_block->block_id] = pattern_block;
	pattern_block->block_id = OPEN;
	connection = pattern_block->connections;
	while (connection) {
		free_pack_pattern(connection->from_block, pattern_block_list);
		free_pack_pattern(connection->to_block, pattern_block_list);
		next = connection->next;
		free(connection);
		connection = next;
	}
}

/**
 * Given a pattern and a atom block to serve as the root block, determine if 
 * the candidate atom block serving as the root node matches the pattern.
 * If yes, return the molecule with this atom block as the root, if not, return NULL
 *
 * Limitations: Currently assumes that forced pack nets must be single-fanout as 
 *              this covers all the reasonable architectures we wanted. More complicated 
 *              structures should probably be handled either downstream (general packing) 
 *              or upstream (in tech mapping).
 *              If this limitation is too constraining, code is designed so that this limitation can be removed
 *
 * Side Effect: If successful, link atom to molecule
 */
static t_pack_molecule *try_create_molecule(
		t_pack_patterns *list_of_pack_patterns, 
        std::multimap<AtomBlockId,t_pack_molecule*>& atom_molecules,
        const int pack_pattern_index,
		AtomBlockId blk_id) {
	int i;
	t_pack_molecule *molecule;

	bool failed = false;

	{
		molecule = (t_pack_molecule*)vtr::calloc(1, sizeof(t_pack_molecule));
		molecule->valid = true;
		molecule->type = MOLECULE_FORCED_PACK;
		molecule->pack_pattern = &list_of_pack_patterns[pack_pattern_index];
		if (molecule->pack_pattern == NULL) {failed = true; goto end_prolog;}

        molecule->atom_block_ids = std::vector<AtomBlockId>(molecule->pack_pattern->num_blocks); //Initializes invalid

		molecule->num_blocks = list_of_pack_patterns[pack_pattern_index].num_blocks;
		if (molecule->num_blocks == 0) {failed = true; goto end_prolog;}

		if (list_of_pack_patterns[pack_pattern_index].root_block == NULL) {failed = true; goto end_prolog;}
		molecule->root = list_of_pack_patterns[pack_pattern_index].root_block->block_id;
		molecule->num_ext_inputs = 0;

		if(list_of_pack_patterns[pack_pattern_index].is_chain == true) {
			/* A chain pattern extends beyond a single logic block so we must find 
             * the blk_id that matches with the portion of a chain for this particular logic block */
			blk_id = find_new_root_atom_for_chain(blk_id, &list_of_pack_patterns[pack_pattern_index], atom_molecules);
		}
	}

	end_prolog:

	if (!failed && blk_id && try_expand_molecule(molecule, atom_molecules, blk_id,
			molecule->pack_pattern->root_block) == true) {
		/* Success! commit module */
		for (i = 0; i < molecule->pack_pattern->num_blocks; i++) {
            auto blk_id2 = molecule->atom_block_ids[i];
			if(!blk_id2) {
				VTR_ASSERT(list_of_pack_patterns[pack_pattern_index].is_block_optional[i] == true);
				continue;
			}			

            atom_molecules.insert({blk_id2, molecule});
		}
	} else {
		failed = true;
	}

	if (failed == true) {
		/* Does not match pattern, free molecule */
		free(molecule);
		molecule = NULL;
	}
	return molecule;
}

/**
 * Determine if atom block can match with the pattern to form a molecule
 * return true if it matches, return false otherwise
 */
static bool try_expand_molecule(t_pack_molecule *molecule,
        const std::multimap<AtomBlockId,t_pack_molecule*>& atom_molecules,
		const AtomBlockId blk_id,
		const t_pack_pattern_block *current_pattern_block) {
	int ipin;
	bool success;
	bool is_optional;
	bool *is_block_optional;
	t_pack_pattern_connections *cur_pack_pattern_connection;
	is_block_optional = molecule->pack_pattern->is_block_optional;
	is_optional = is_block_optional[current_pattern_block->block_id];

    /* If the block in the pattern has already been visited, then there is no need to revisit it */
	if (molecule->atom_block_ids[current_pattern_block->block_id]) {
		if (molecule->atom_block_ids[current_pattern_block->block_id] != blk_id) {
			/* Mismatch between the visited block and the current block implies 
             * that the current netlist structure does not match the expected pattern, 
             * return whether or not this matters */
			return is_optional;
		} else {
			molecule->num_ext_inputs--; /* This block is revisited, implies net is entirely internal to molecule, reduce count */
			return true;
		}
	}

	/* This node has never been visited */
	/* Simplifying assumption: An atom can only map to one molecule */
    auto rng = atom_molecules.equal_range(blk_id);
    bool rng_empty = rng.first == rng.second;
	if(!rng_empty) {
		/* This block is already in a molecule, return whether or not this matters */
		return is_optional;
	}

	if (primitive_type_feasible(blk_id, current_pattern_block->pb_type)) {

		success = true;
		/* If the primitive types match, store it, expand it and explore neighbouring nodes */


        /* store that this node has been visited */
		molecule->atom_block_ids[current_pattern_block->block_id] = blk_id;

		molecule->num_ext_inputs += num_used_pins(g_atom_nl, g_atom_nl.block_input_ports(blk_id));
		
		cur_pack_pattern_connection = current_pattern_block->connections;
		while (cur_pack_pattern_connection != NULL && success == true) {
			if (cur_pack_pattern_connection->from_block == current_pattern_block) {
				/* find net corresponding to pattern */
                auto port_id = g_atom_nl.find_port(blk_id, cur_pack_pattern_connection->from_pin->port->model_port->name);
                VTR_ASSERT(port_id);
				ipin = cur_pack_pattern_connection->from_pin->pin_number;
                auto net_id = g_atom_nl.port_net(port_id, ipin);

                auto net_sinks = g_atom_nl.net_sinks(net_id);

				/* Check if net is valid */
				if (!net_id || net_sinks.size() != 1) { /* One fanout assumption */
					success = is_block_optional[cur_pack_pattern_connection->to_block->block_id];
				} else {
                    VTR_ASSERT(net_sinks.size() == 1);

                    auto sink_pin_id = *net_sinks.begin();
                    auto sink_blk_id = g_atom_nl.pin_block(sink_pin_id);

					success = try_expand_molecule(molecule, atom_molecules, sink_blk_id,
                                cur_pack_pattern_connection->to_block);
				}
			} else {
				VTR_ASSERT(cur_pack_pattern_connection->to_block == current_pattern_block);
				/* find net corresponding to pattern */

                auto port_id = g_atom_nl.find_port(blk_id, cur_pack_pattern_connection->to_pin->port->model_port->name);
                VTR_ASSERT(port_id);
				ipin = cur_pack_pattern_connection->to_pin->pin_number;

                AtomNetId net_id;
				if (cur_pack_pattern_connection->to_pin->port->model_port->is_clock) {
                    VTR_ASSERT(ipin == 1); //TODO: support multi-clock primitives
					net_id = g_atom_nl.port_net(port_id, ipin);
				} else {
					net_id = g_atom_nl.port_net(port_id, ipin);
				}
				/* Check if net is valid */
				if (!net_id || g_atom_nl.net_sinks(net_id).size() != 1) { /* One fanout assumption */
					success = is_block_optional[cur_pack_pattern_connection->from_block->block_id];
				} else {
                    auto driver_blk_id = g_atom_nl.net_driver_block(net_id);
					success = try_expand_molecule(molecule, atom_molecules, driver_blk_id,
                                cur_pack_pattern_connection->from_block);
				}
			}
			cur_pack_pattern_connection = cur_pack_pattern_connection->next;
		}
	} else {
		success = is_optional;
	}

	return success;
}

static void print_pack_molecules(const char *fname,
		const t_pack_patterns *list_of_pack_patterns, const int num_pack_patterns,
		const t_pack_molecule *list_of_molecules) {
	int i;
	FILE *fp;
	const t_pack_molecule *list_of_molecules_current;

	fp = std::fopen(fname, "w");
	fprintf(fp, "# of pack patterns %d\n", num_pack_patterns);
		
	for (i = 0; i < num_pack_patterns; i++) {
		fprintf(fp, "pack pattern index %d block count %d name %s root %s\n",
				list_of_pack_patterns[i].index,
				list_of_pack_patterns[i].num_blocks,
				list_of_pack_patterns[i].name,
				list_of_pack_patterns[i].root_block->pb_type->name);
	}

	list_of_molecules_current = list_of_molecules;
	while (list_of_molecules_current != NULL) {
		if (list_of_molecules_current->type == MOLECULE_SINGLE_ATOM) {
			fprintf(fp, "\nmolecule type: atom\n");
			fprintf(fp, "\tpattern index %d: atom block %s\n", i,
					g_atom_nl.block_name(list_of_molecules_current->atom_block_ids[0]).c_str());
		} else if (list_of_molecules_current->type == MOLECULE_FORCED_PACK) {
			fprintf(fp, "\nmolecule type: %s\n",
					list_of_molecules_current->pack_pattern->name);
			for (i = 0; i < list_of_molecules_current->pack_pattern->num_blocks;
					i++) {
				if(!list_of_molecules_current->atom_block_ids[i]) {
					fprintf(fp, "\tpattern index %d: empty \n",	i);
				} else {
					fprintf(fp, "\tpattern index %d: atom block %s",
						i,
						g_atom_nl.block_name(list_of_molecules_current->atom_block_ids[i]).c_str());
					if(list_of_molecules_current->pack_pattern->root_block->block_id == i) {
						fprintf(fp, " root node\n");
					} else {
						fprintf(fp, "\n");
					}
				}
			}
		} else {
			VTR_ASSERT(0);
		}
		list_of_molecules_current = list_of_molecules_current->next;
	}

	fclose(fp);
}

/* Search through all primitives and return the lowest cost primitive that fits this atom block */
static t_pb_graph_node* get_expected_lowest_cost_primitive_for_atom_block(const AtomBlockId blk_id) {
	int i;
	float cost, best_cost;
	t_pb_graph_node *current, *best;

	best_cost = UNDEFINED;
	best = NULL;
	current = NULL;
	for(i = 0; i < num_types; i++) {
		cost = UNDEFINED;
		current = get_expected_lowest_cost_primitive_for_atom_block_in_pb_graph_node(blk_id, type_descriptors[i].pb_graph_head, &cost);
		if(cost != UNDEFINED) {
			if(best_cost == UNDEFINED || best_cost > cost) {
				best_cost = cost;
				best = current;
			}
		}
	}
	return best;
}

static t_pb_graph_node *get_expected_lowest_cost_primitive_for_atom_block_in_pb_graph_node(const AtomBlockId blk_id, t_pb_graph_node *curr_pb_graph_node, float *cost) {
	t_pb_graph_node *best, *cur;
	float cur_cost, best_cost;
	int i, j;

	best = NULL;
	best_cost = UNDEFINED;
	if(curr_pb_graph_node == NULL) {
		return NULL;
	}

	if(curr_pb_graph_node->pb_type->blif_model != NULL) {
		if(primitive_type_feasible(blk_id, curr_pb_graph_node->pb_type)) {
			cur_cost = compute_primitive_base_cost(curr_pb_graph_node);
			if(best_cost == UNDEFINED || best_cost > cur_cost) {
				best_cost = cur_cost;
				best = curr_pb_graph_node;
			}
		}
	} else {
		for(i = 0; i < curr_pb_graph_node->pb_type->num_modes; i++) {
			for(j = 0; j < curr_pb_graph_node->pb_type->modes[i].num_pb_type_children; j++) {
				*cost = UNDEFINED;
				cur = get_expected_lowest_cost_primitive_for_atom_block_in_pb_graph_node(blk_id, &curr_pb_graph_node->child_pb_graph_nodes[i][j][0], cost);
				if(cur != NULL) {
					if(best == NULL || best_cost > *cost) {
						best = cur;
						best_cost = *cost;
					}
				}
			}
		}
	}

	*cost = best_cost;
	return best;
}


/* Determine which of two pack pattern should take priority */
static int compare_pack_pattern(const t_pack_patterns *pattern_a, const t_pack_patterns *pattern_b) {
	float base_gain_a, base_gain_b, diff;

	/* Bigger patterns should take higher priority than smaller patterns because they are harder to fit */
	if (pattern_a->num_blocks > pattern_b->num_blocks) {
		return 1;
	} else if (pattern_a->num_blocks < pattern_b->num_blocks) {
		return -1;
	}

	base_gain_a = pattern_a->base_cost;
	base_gain_b = pattern_b->base_cost;
	diff = base_gain_a - base_gain_b;

	/* Less costly patterns should be used before more costly patterns */
	if (diff < 0) {
		return 1;
	}
	if (diff > 0) {
		return -1;
	}
	return 0;
}

/* A chain can extend across multiple atom blocks.  Must segment the chain to fit in an atom 
 * block by identifying the actual atom that forms the root of the new chain.
 * Returns AtomBlockId::INVALID() if this block_index doesn't match up with any chain
 *
 * Assumes that the root of a chain is the primitive that starts the chain or is driven from outside the logic block
 * block_index: index of current atom
 * list_of_pack_pattern: ptr to current chain pattern
 */
static AtomBlockId find_new_root_atom_for_chain(const AtomBlockId blk_id, const t_pack_patterns *list_of_pack_pattern,
        const std::multimap<AtomBlockId,t_pack_molecule*>& atom_molecules) {
    AtomBlockId new_root_blk_id;
	t_pb_graph_pin *root_ipin;
	t_pb_graph_node *root_pb_graph_node;
	t_model_ports *model_port;
	
	VTR_ASSERT(list_of_pack_pattern->is_chain == true);
	root_ipin = list_of_pack_pattern->chain_root_pin;
	root_pb_graph_node = root_ipin->parent_node;

	if(primitive_type_feasible(blk_id, root_pb_graph_node->pb_type) == false) {
		return AtomBlockId::INVALID();
	}

	/* Assign driver furthest up the chain that matches the root node and is unassigned to a molecule as the root */
	model_port = root_ipin->port->model_port;

    auto port_id = g_atom_nl.find_port(blk_id, model_port->name);
    VTR_ASSERT(port_id);

	AtomNetId driver_net_id = g_atom_nl.port_net(port_id, root_ipin->pin_number);
	if(!driver_net_id) {
		/* The current block is the furthest up the chain, return it */
		return blk_id;
	}

	AtomBlockId driver_blk_id = g_atom_nl.port_block(port_id);

    auto rng = atom_molecules.equal_range(blk_id);
    bool rng_empty = (rng.first == rng.second);
	if(!rng_empty) {
		/* Driver is used/invalid, so current block is the furthest up the chain, return it */
		return blk_id;
	}

	new_root_blk_id = find_new_root_atom_for_chain(driver_blk_id, list_of_pack_pattern, atom_molecules);
	if(!new_root_blk_id) {
		return blk_id;
	} else {
		return new_root_blk_id;
	}
}



